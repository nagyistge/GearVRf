/* Copyright 2015 Samsung Electronics Co., LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "batch.h"
#include "objects/scene_object.h"
#include "objects/components/camera.h"
#include "objects/components/render_data.h"
#include "objects/light.h"

#define BATCH_SIZE 60

namespace gvr {
Batch::Batch(int no_vertices, int no_indices) :
        draw_count_(0), vertex_count_(0), index_count_(0), vertex_limit_(no_vertices),
        indices_limit_(no_indices), renderdata_(nullptr),mesh_init_(false),
        index_offset_(0), not_batched_(false), batch_dirty_(false) {

    vertices_.reserve(no_vertices);
    indices_.reserve(no_indices);
    normals_.reserve(no_vertices);
    tex_coords_.reserve(no_vertices);
    matrix_indices_.reserve(no_vertices);
}

Batch::~Batch() {
    clearData();
    delete renderdata_;
    renderdata_ = nullptr;
}

bool Batch::updateMesh(Mesh* render_mesh){
    const std::vector<unsigned short>& indices = render_mesh->indices();
    const std::vector<glm::vec3>& vertices = render_mesh->vertices();
    const std::vector<glm::vec3>& normals = render_mesh->normals();
    const std::vector<glm::vec2>& tex_cords = render_mesh->getVec2Vector("a_texcoord");

    int size = 0;
    size = vertices.size();

    for(int i=0;i<size;i++){
        vertices_.push_back(vertices[i]);
        matrix_indices_.push_back(draw_count_);
        tex_coords_.push_back(tex_cords[i]);
    }
    // Check if models has normals
    if(normals.size() > 0){
        int normals_size = normals.size();
        for(int i=0;i<normals_size;i++)
            normals_.push_back(normals[i]);
    }
    size = indices.size();
    index_count_+=size;
    for (int i = 0; i < size; i++) {
        unsigned short index = indices[i];
        index += index_offset_;
        indices_.push_back(index);
    }
    // update all VBO data
    vertex_count_ += vertices.size();
    index_offset_ += vertices.size();
    draw_count_++;
    mesh_init_ = false;
}
/*
 * Add renderdata of scene object into mesh, add vertices, texcoords, normals, model matrices
 */
bool Batch::add(RenderData *render_data) {
    material_ = render_data->pass(0)->material();
    Mesh *render_mesh = render_data->mesh();
    const std::vector<unsigned short>& indices = render_mesh->indices();

    Transform* const t = render_data->owner_object()->transform();
    glm::mat4 model_matrix;
    if (t != NULL) {
        model_matrix = glm::mat4(t->getModelMatrix());
    }
    render_data->getHashCode();
    render_data->set_renderdata_dirty(false);

    // Store the model matrix and its index into map for update
    matrix_index_map_[render_data] = draw_count_;
    matrices_.push_back(model_matrix);
    render_data->owner_object()->setTransformUnDirty();

    if(!render_data->batching()){
        render_data_set_.insert(render_data);
        not_batched_ = true;
        return true;
    }

    // if it is not texture shader, dont add into batch, render in normal way
    for(int i=0; i<render_data->pass_count();i++)
    {
        Material* mat = render_data->pass(i)->material();
        if (mat->shader_type() != Material::ShaderType::TEXTURE_SHADER ) {
            render_data_set_.insert(render_data);
            return true;
        }
    }
    // if mesh is large, render in normal way
    if (indices.size() == 0 || (indices.size() + index_count_ > indices_limit_)) {
        if (draw_count_ > 0) {
            return false;
        } else {
            render_data_set_.insert(render_data);
            render_data->set_batching(false);
            not_batched_ = true;
            return true;
        }
    }
    // Copy all renderData properties
    if (draw_count_ == 0) {
        if (!renderdata_) {
            renderdata_ = new RenderData(*render_data);
            renderdata_->set_batching(true);
       }
    }
    render_data_set_.insert(render_data); // store all the renderdata which are in batch
    updateMesh(render_mesh);
    return true;
}
void Batch::clearData(){
    vertex_count_ = 0;
    index_count_ = 0;
    index_offset_ = 0;
    draw_count_=0;
    matrix_index_map_.clear();
    matrix_indices_.clear();
    matrices_.clear();
    tex_coords_.clear();
    vertices_.clear();
    normals_.clear();
    indices_.clear();
    mesh_init_ = false;
    batch_dirty_ = false;
}

bool Batch::isRenderModified(){
     bool update_vbo = false;
     for(auto it= render_data_set_.begin();it!=render_data_set_.end();){
        if(!(*it)->enabled() || !(*it)->owner_object()->enabled()){
            (*it)->set_batching(false);
            (*it)->setBatchNull();
            render_data_set_.erase(it++);
            update_vbo = true;
        }
        else {
            ++it;
        }
    }
    return update_vbo;
}
void Batch::resetBatch(){
    clearData();
    delete renderdata_;
    renderdata_ = nullptr;
    gRenderer->freeBatch(this);
}

void Batch::meshInit(){
    mesh_init_ = true;
    mesh_.set_vertices(vertices_);
    mesh_.set_normals(normals_);
    mesh_.setVec2Vector("a_texcoord",tex_coords_);
    mesh_.set_indices(indices_);
    mesh_.setFloatVector("a_matrix_index", matrix_indices_);
    if (nullptr != renderdata_) {
        renderdata_->set_mesh(&mesh_);
    }
}
bool Batch::setupMesh(bool batch_dirty){
    bool update_vbo = isRenderModified();
    // batch is empty, add it back to the pool
    if(0 == render_data_set_.size()){
        resetBatch();
        return false;
    }
    if(batch_dirty || update_vbo)
        regenerateMeshData();
    batch_dirty_ = false;
    if(!mesh_init_)
        meshInit();
    return true;
}
void Batch::regenerateMeshData(){
    clearData();
    for(auto it= render_data_set_.begin();it!=render_data_set_.end();++it){
        RenderData* render_data = *it;
        Mesh *render_mesh = render_data->mesh();
        const std::vector<unsigned short>& indices = render_mesh->indices();
        Transform* const t = render_data->owner_object()->transform();
        glm::mat4 model_matrix;
        if (t != NULL) {
            model_matrix = glm::mat4(t->getModelMatrix());
        }
        // Store the model matrix and its index into map for update
        matrix_index_map_[render_data] = draw_count_;
        matrices_.push_back(model_matrix);
        updateMesh(render_mesh);
    }
}
}
