// Copyright [2015] Albert Huang

#include "sceneview/internal_gl.hpp"

#include "sceneview/draw_context.hpp"

#include <cmath>
#include <vector>

#include <QOpenGLTexture>

#include "sceneview/camera_node.hpp"
#include "sceneview/group_node.hpp"
#include "sceneview/light_node.hpp"
#include "sceneview/draw_node.hpp"
#include "sceneview/resource_manager.hpp"
#include "sceneview/renderer.hpp"
#include "sceneview/scene_node.hpp"
#include "sceneview/stock_resources.hpp"

#if 0
#define dbg(fmt, ...) printf(fmt, __VA_ARGS__)
#else
#define dbg(...)
#endif

namespace sv {

static void CheckGLErrors(const QString& name) {
  GLenum err_code = glGetError();
  const char *err_str;
  while (err_code != GL_NO_ERROR) {
    err_str = sv::glErrorString(err_code);
    fprintf(stderr, "OpenGL Error (%s)\n", name.toStdString().c_str());
    fprintf(stderr, "%s\n", err_str);
    err_code = glGetError();
  }
}

DrawContext::DrawContext(const ResourceManager::Ptr& resources,
    const Scene::Ptr& scene) :
  resources_(resources),
  scene_(scene),
  clear_color_(0, 0, 0, 255),
  cur_camera_(nullptr),
  bounding_box_node_(nullptr),
  draw_bounding_boxes_(false) {}

void DrawContext::Draw(CameraNode* camera, std::vector<Renderer*>* prenderers) {
  cur_camera_ = camera;

  // Clear the drawing area
  glClearColor(clear_color_.redF(),
      clear_color_.greenF(),
      clear_color_.blueF(),
      clear_color_.alphaF());

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  std::vector<Renderer*>& renderers = *prenderers;

  // Setup the fixed-function pipeline.
  PrepareFixedFunctionPipeline();

  // Inform the renderers that drawing is about to begin
  for (Renderer* renderer : renderers) {
    if (renderer->Enabled()) {
      glPushAttrib(GL_ENABLE_BIT | GL_POINT_BIT | GL_POLYGON_STIPPLE_BIT |
          GL_POLYGON_BIT | GL_LINE_BIT | GL_FOG_BIT | GL_LIGHTING_BIT);
      glMatrixMode(GL_MODELVIEW);
      glPushMatrix();
      renderer->RenderBegin();
      CheckGLErrors(renderer->Name());
      glMatrixMode(GL_MODELVIEW);
      glPopMatrix();
      glPopAttrib();
    }
  }

  // TODO(albert) sort the draw nodes

  // render each draw node
  for (DrawNode* draw_node : scene_->DrawNodes()) {
    if (draw_node->Visible()) {
      DrawDrawNode(draw_node);
    }

    if (draw_bounding_boxes_) {
      const AxisAlignedBox box_orig = draw_node->GeometryBoundingBox();
      const AxisAlignedBox box = box_orig.Transformed(model_mat_);
      DrawBoundingBox(box);
    }
  }

  // Setup the fixed-function pipeline again.
  PrepareFixedFunctionPipeline();

  // Notify renderers that drawing has finished
  for (Renderer* renderer : renderers) {
    if (renderer->Enabled()) {
      glPushAttrib(GL_ENABLE_BIT | GL_POINT_BIT | GL_POLYGON_STIPPLE_BIT |
          GL_POLYGON_BIT | GL_LINE_BIT | GL_FOG_BIT | GL_LIGHTING_BIT);
      glMatrixMode(GL_MODELVIEW);
      glPushMatrix();
      renderer->RenderEnd();
      CheckGLErrors(renderer->Name());
      glMatrixMode(GL_MODELVIEW);
      glPopMatrix();
      glPopAttrib();
    }
  }

  cur_camera_ = nullptr;
}

void DrawContext::PrepareFixedFunctionPipeline() {
  // Enable the fixed function pipeline by disabling any active shader program.
  glUseProgram(0);

  // Setup the projection and view matrices
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMultMatrixf(cur_camera_->GetProjectionMatrix().constData());
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glMultMatrixf(cur_camera_->GetViewMatrix().constData());

  // Setup lights
  const GLenum gl_lights[] = {
    GL_LIGHT0, GL_LIGHT1, GL_LIGHT2, GL_LIGHT3,
    GL_LIGHT4, GL_LIGHT5, GL_LIGHT6, GL_LIGHT7
  };
  std::vector<LightNode*> lights = scene_->Lights();
  for (int light_ind = 0; light_ind < 8; ++light_ind) {
    const GLenum gl_light = gl_lights[light_ind];
    LightNode* light = lights[light_ind];
    const LightType light_type = light->GetLightType();

    if (light_type == LightType::kDirectional) {
      const QVector3D dir = light->Direction();
      const float dir4f[4] = { dir.x(), dir.y(), dir.z(), 0 };
      glLightfv(gl_light, GL_POSITION, dir4f);
    } else {
      const QVector3D posf = light->Translation();
      const float pos4f[4] = { posf.x(), posf.y(), posf.z(), 1};
      glLightfv(gl_light, GL_POSITION, pos4f);

      const float attenuation = light->Attenuation();
      glLightf(gl_light, GL_QUADRATIC_ATTENUATION, attenuation);
      glLightf(gl_light, GL_CONSTANT_ATTENUATION, 1.0);

      if (light_type == LightType::kSpot) {
        const float cone_angle_deg = light->ConeAngle();
        glLightf(gl_light, GL_SPOT_CUTOFF, cone_angle_deg);
        glLightf(gl_light, GL_SPOT_EXPONENT, 1.2);
      }
    }

    const QVector3D& color = light->Color();
    const QVector3D& ambient = color * light->Ambient();
    const float color4f[4] = { color.x(),  color.y(),  color.z(), 1 };
    const float ambient4f[4] = { ambient.x(),  ambient.y(),  ambient.z(), 1 };
    glLightfv(gl_light, GL_AMBIENT, ambient4f);
    glLightfv(gl_light, GL_DIFFUSE, color4f);
    glLightfv(gl_light, GL_SPECULAR, color4f);

    glEnable(gl_light);
    break;
  }

  // Set some default rendering parameters
  glFrontFace(GL_CCW);
  glCullFace(GL_BACK);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
}

void DrawContext::DrawDrawNode(DrawNode* draw_node) {
  // Compute the model matrix and check visibility
  model_mat_ = draw_node->GetTransform();
  for (SceneNode* node = draw_node->ParentNode(); node; node = node->ParentNode()) {
    model_mat_ = node->GetTransform() * model_mat_;
    if (!node->Visible()) {
      return;
    }
  }

  for (const Drawable::Ptr& drawable : draw_node->Drawables()) {
    geometry_ = drawable->Geometry();
    material_ = drawable->Material();
    shader_ = material_->Shader();

    // Activate the shader program
    if (!shader_) {
      continue;
    }
    program_ = shader_->Program();
    if (!program_) {
      continue;
    }

    ActivateMaterial();

    if (drawable->PreDraw()) {
      DrawGeometry();
    }

    drawable->PostDraw();

    GLenum gl_err = glGetError();
    if (gl_err != GL_NO_ERROR) {
      printf("OpenGL: %s\n", sv::glErrorString(gl_err));
    }

    // Done. Release resources
    program_->release();

    // If we called glPointSize() earlier, then reset the value.
    if (material_->PointSize() > 0) {
      glPointSize(1);
    }

    if (material_->LineWidth() > 0) {
      glLineWidth(1);
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  }
}

void DrawContext::ActivateMaterial() {
  program_->bind();

  glFrontFace(GL_CCW);

  // set OpenGL attributes based on material properties.
  if (material_->TwoSided()) {
    glDisable(GL_CULL_FACE);
  } else {
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
  }

  if (material_->DepthTest()) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }

  if (material_->DepthWrite()) {
    glDepthMask(GL_TRUE);
  } else {
    glDepthMask(GL_FALSE);
  }

  if (material_->ColorWrite()) {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  } else {
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  }

  const float point_size = material_->PointSize();
  if (point_size > 0) {
    glPointSize(point_size);
  }

  const float line_width = material_->LineWidth();
  if (line_width > 0) {
    glLineWidth(line_width);
  }

  if (material_->Blend()) {
    glEnable(GL_BLEND);
    GLenum sfactor;
    GLenum dfactor;
    material_->BlendFunc(&sfactor, &dfactor);
    glBlendFunc(sfactor, dfactor);
  } else {
    glDisable(GL_BLEND);
  }

  // Set shader standard variables
  const ShaderStandardVariables& locs = shader_->StandardVariables();
  const QMatrix4x4 proj_mat = cur_camera_->GetProjectionMatrix();
  const QMatrix4x4 view_mat = cur_camera_->GetViewMatrix();

  // Set uniform variables
  if (locs.sv_proj_mat >= 0) {
    program_->setUniformValue(locs.sv_proj_mat, proj_mat);
  }
  if (locs.sv_view_mat >= 0) {
    program_->setUniformValue(locs.sv_view_mat, view_mat);
  }
  if (locs.sv_view_mat_inv >= 0) {
    program_->setUniformValue(locs.sv_view_mat_inv, view_mat.inverted());
  }
  if (locs.sv_model_mat >= 0) {
    program_->setUniformValue(locs.sv_model_mat, model_mat_);
  }
  if (locs.sv_mvp_mat >= 0) {
    program_->setUniformValue(locs.sv_mvp_mat,
        proj_mat * view_mat * model_mat_);
  }
  if (locs.sv_mv_mat >= 0) {
    program_->setUniformValue(locs.sv_mv_mat, view_mat * model_mat_);
  }
  if (locs.sv_model_normal_mat >= 0) {
    program_->setUniformValue(locs.sv_model_normal_mat,
        model_mat_.normalMatrix());
  }

  const std::vector<LightNode*>& lights = scene_->Lights();
  int num_lights = lights.size();
  if (num_lights > kShaderMaxLights) {
    printf("Too many lights. Max: %d\n", kShaderMaxLights);
    num_lights = kShaderMaxLights;
  }

  for (int light_ind = 0; light_ind < num_lights; ++light_ind) {
    const LightNode* light_node = lights[light_ind];
    const ShaderLightLocation light_loc = locs.sv_lights[light_ind];
    LightType light_type = light_node->GetLightType();

    if (light_loc.is_directional >= 0) {
      const bool is_directional = light_type == LightType::kDirectional;
      program_->setUniformValue(light_loc.is_directional, is_directional);
    }

    if (light_loc.direction >= 0) {
      const QVector3D light_dir = light_node->Direction();
      program_->setUniformValue(light_loc.direction, light_dir);
    }

    if (light_loc.position >= 0) {
      const QVector3D light_pos = light_node->Translation();
      program_->setUniformValue(light_loc.position, light_pos);
    }

    if (light_loc.ambient >= 0) {
      const float ambient = light_node->Ambient();
      program_->setUniformValue(light_loc.ambient, ambient);
    }

    if (light_loc.color >= 0) {
      const QVector3D color = light_node->Color();
      program_->setUniformValue(light_loc.color, color);
    }

    if (light_loc.attenuation >= 0) {
      const float attenuation = light_node->Attenuation();
      program_->setUniformValue(light_loc.attenuation, attenuation);
    }

    if (light_loc.cone_angle >= 0) {
      const float cone_angle = light_node->ConeAngle() * M_PI / 180;
      program_->setUniformValue(light_loc.cone_angle, cone_angle);
    }
  }

  // Load shader uniform variables from the material
  for (auto& item : material_->ShaderParameters()) {
    ShaderUniform& uniform = item.second;
    uniform.LoadToProgram(program_);
  }

  // Load textures
  unsigned int texunit = 0;
  for (auto& item : *(material_->GetTextures())) {
    const QString& texname = item.first;
    QOpenGLTexture* texture = item.second;
    texture->bind(texunit);
    program_->setUniformValue(texname.toStdString().c_str(), texunit);
  }
}

static void SetupAttributeArray(QOpenGLShaderProgram* program,
    int location, int num_attributes,
    GLenum attr_type, int offset, int attribute_size) {
  if (location < 0) {
    return;
  }

  if (num_attributes > 0) {
    program->enableAttributeArray(location);
    program->setAttributeBuffer(location,
        attr_type, offset, attribute_size, 0);
  } else {
    program->disableAttributeArray(location);
  }
}

void DrawContext::DrawGeometry() {
  // Load geometry and bind a vertex buffer
  QOpenGLBuffer* vbo = geometry_->VBO();
  vbo->bind();

  // Load per-vertex attribute arrays
  const ShaderStandardVariables& locs = shader_->StandardVariables();
  SetupAttributeArray(program_, locs.sv_vert_pos,
      geometry_->NumVertices(), GL_FLOAT, geometry_->VertexOffset(), 3);
  SetupAttributeArray(program_, locs.sv_normal,
      geometry_->NumNormals(), GL_FLOAT, geometry_->NormalOffset(), 3);
  SetupAttributeArray(program_, locs.sv_diffuse,
      geometry_->NumDiffuse(), GL_FLOAT, geometry_->DiffuseOffset(), 4);
  SetupAttributeArray(program_, locs.sv_specular,
      geometry_->NumSpecular(), GL_FLOAT, geometry_->SpecularOffset(), 4);
  SetupAttributeArray(program_, locs.sv_shininess,
      geometry_->NumShininess(), GL_FLOAT, geometry_->ShininessOffset(), 1);
  SetupAttributeArray(program_, locs.sv_tex_coords_0,
      geometry_->NumTexCoords0(), GL_FLOAT, geometry_->TexCoords0Offset(), 2);

  // TODO load custom attribute arrays

  // Draw the geometry
  QOpenGLBuffer* index_buffer = geometry_->IndexBuffer();
  if (index_buffer) {
    index_buffer->bind();
    glDrawElements(geometry_->GLMode(), geometry_->NumIndices(),
        geometry_->IndexType(), 0);
    index_buffer->release();
  } else {
    glDrawArrays(geometry_->GLMode(), 0, geometry_->NumVertices());
  }
  vbo->release();
}

void DrawContext::DrawBoundingBox(const AxisAlignedBox& box) {
  if (!bounding_box_node_) {
    StockResources stock(resources_);
    ShaderResource::Ptr shader =
      stock.Shader(StockResources::kUniformColorNoLighting);

    MaterialResource::Ptr material = resources_->MakeMaterial(shader);
    material->SetParam("color", 0.0f, 1.0f, 0.0f, 1.0f);

    GeometryResource::Ptr geometry = resources_->MakeGeometry();
    GeometryData gdata;
    gdata.gl_mode = GL_LINES;
    gdata.vertices = {
      { QVector3D(0, 0, 0) },
      { QVector3D(0, 1, 0) },
      { QVector3D(1, 1, 0) },
      { QVector3D(1, 0, 0) },
      { QVector3D(0, 0, 1) },
      { QVector3D(0, 1, 1) },
      { QVector3D(1, 1, 1) },
      { QVector3D(1, 0, 1) },
    };
    gdata.indices = {
      0, 1, 1, 2, 2, 3, 3, 0,
      4, 5, 5, 6, 6, 7, 7, 4,
      0, 4, 1, 5, 2, 6, 3, 7 };
    geometry->Load(gdata);

    bounding_box_node_ = scene_->MakeDrawNode(nullptr);
    bounding_box_node_->Add(geometry, material);

    // hack to prevent the bounding box to appear during normal rendering
    bounding_box_node_->SetVisible(false);
  }

  bounding_box_node_->SetScale(box.Max() - box.Min());
  bounding_box_node_->SetTranslation(box.Min());

  DrawDrawNode(bounding_box_node_);
}

}  // namespace sv