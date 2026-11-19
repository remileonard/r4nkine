// gltf_loader.cpp - glTF 2.0 (.gltf with its buffers, .glb), with cgltf (MIT, the
// cgltf port of vcpkg).
//
// Read: the node hierarchy (matrices or translation, rotation, scale), the
// meshes and all their primitive modes, positions, normals, the first
// texture coordinates and colors, indices; the materials: base color and
// its texture, emission, alpha modes, double-sided, KHR_materials_unlit. The
// metallic-roughness model is approximated by Blinn-Phong until the PBR
// shading comes. The images may be files, data URIs or buffer views. glTF
// is Y-up: the root turns +Y into +Z.
#include "r4/axes.h"
#include "r4/assets/model.h"

#define CGLTF_IMPLEMENTATION
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "cgltf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace r4::assets {
namespace {

std::string resultText(cgltf_result r) {
    switch (r) {
    case cgltf_result_data_too_short:
        return "data too short";
    case cgltf_result_unknown_format:
        return "unknown format";
    case cgltf_result_invalid_json:
        return "invalid JSON";
    case cgltf_result_invalid_gltf:
        return "invalid glTF";
    case cgltf_result_invalid_options:
        return "invalid options";
    case cgltf_result_file_not_found:
        return "file not found";
    case cgltf_result_io_error:
        return "I/O error";
    case cgltf_result_out_of_memory:
        return "out of memory";
    case cgltf_result_legacy_gltf:
        return "glTF 1.0 is not supported";
    default:
        return "error";
    }
}

uint32_t packColor(const float *c, int n) {
    auto b = [](float x) {
        return (uint32_t)(std::fmin(std::fmax(x, 0.0f), 1.0f) * 255 + 0.5f);
    };
    return b(c[0]) | b(c[1]) << 8 | b(c[2]) << 16 | (n > 3 ? b(c[3]) : 255u) << 24;
}

// the image of a glTF image: a buffer view, a data URI or a file
bool readImage(const cgltf_image &im, const std::string &dir, const cgltf_options &opts, Image &out, std::string *err) {
    if (im.buffer_view && im.buffer_view->buffer && im.buffer_view->buffer->data) {
        const uint8_t *p = (const uint8_t *)im.buffer_view->buffer->data + im.buffer_view->offset;
        return decodeImage(p, im.buffer_view->size, out, err);
    }
    if (!im.uri) {
        return false;
    }
    std::string uri = im.uri;
    if (uri.compare(0, 5, "data:") == 0) {
        size_t comma = uri.find(";base64,");
        if (comma == std::string::npos) {
            return false;
        }
        const char *b64 = uri.c_str() + comma + 8;
        size_t len = std::strlen(b64);
        size_t pad = 0;
        if (len && b64[len - 1] == '=') {
            pad++;
        }
        if (len > 1 && b64[len - 2] == '=') {
            pad++;
        }
        size_t size = len / 4 * 3 - pad;
        void *data = nullptr;
        if (cgltf_load_buffer_base64(&opts, size, b64, &data) != cgltf_result_success) {
            return false;
        }
        bool ok = decodeImage((const uint8_t *)data, size, out, err);
        std::free(data);
        return ok;
    }
    std::vector<char> name(uri.begin(), uri.end());
    name.push_back(0);
    cgltf_decode_uri(name.data());
    std::string path = findAsset(dir, name.data());
    return !path.empty() && loadImage(path, out, err);
}

} // namespace

std::shared_ptr<Model> loadGLTF(const std::string &path, std::string *error) {
    cgltf_options opts{};
    cgltf_data *data = nullptr;
    cgltf_result r = cgltf_parse_file(&opts, path.c_str(), &data);
    if (r == cgltf_result_success) {
        r = cgltf_load_buffers(&opts, data, path.c_str());
    }
    if (r != cgltf_result_success) {
        if (error) {
            *error = path + ": " + resultText(r);
        }
        if (data) {
            cgltf_free(data);
        }
        return nullptr;
    }
    size_t slash = path.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    auto model = std::make_shared<Model>();
    model->source = path;

    // the images, loaded once each
    std::map<const cgltf_image *, int> images;
    auto imageOf = [&](const cgltf_texture *t) -> int {
        if (!t || !t->image) {
            return -1;
        }
        auto it = images.find(t->image);
        if (it != images.end()) {
            return it->second;
        }
        Image img;
        std::string err;
        int index = -1;
        if (readImage(*t->image, dir, opts, img, &err)) {
            img.name = t->image->uri ? t->image->uri : (t->image->name ? t->image->name : "embedded");
            if (t->sampler) {
                img.clampS = t->sampler->wrap_s == 33071; // GL_CLAMP_TO_EDGE
                img.clampT = t->sampler->wrap_t == 33071;
            }
            index = (int)model->images.size();
            model->images.push_back(std::move(img));
        } else {
            std::fprintf(stderr, "r4: glTF: cannot read an image of %s %s\n", path.c_str(), err.c_str());
        }
        images[t->image] = index;
        return index;
    };

    // the materials
    std::map<const cgltf_material *, int> materials;
    for (size_t i = 0; i < data->materials_count; i++) {
        const cgltf_material &gm = data->materials[i];
        Material m;
        m.name = gm.name ? gm.name : "material " + std::to_string(i);
        m.ambient = {1, 1, 1};
        if (gm.has_pbr_metallic_roughness) {
            const auto &pbr = gm.pbr_metallic_roughness;
            m.diffuse = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]};
            m.diffuseImage = imageOf(pbr.base_color_texture.texture);
            // Blinn-Phong from the roughness and metalness, for now
            float rough = std::fmin(std::fmax(pbr.roughness_factor, 0.05f), 1.0f);
            float spec = 0.04f + 0.5f * pbr.metallic_factor * (1 - rough);
            m.specular = {spec, spec, spec};
            m.shininess = std::fmin(2.0f / (rough * rough * rough * rough) - 2.0f, 256.0f);
        }
        m.emissive = {gm.emissive_factor[0], gm.emissive_factor[1], gm.emissive_factor[2]};
        if (gm.unlit) {
            m.shading = Shading::Unlit;
        }
        m.doubleSided = gm.double_sided;
        if (gm.alpha_mode == cgltf_alpha_mode_blend) {
            m.alpha = AlphaMode::Blend;
        } else if (gm.alpha_mode == cgltf_alpha_mode_mask) {
            m.alpha = AlphaMode::Mask;
            m.alphaCutoff = gm.alpha_cutoff;
        }
        materials[&gm] = (int)model->materials.size();
        model->materials.push_back(m);
    }
    // the colored materials (COLOR_0): a copy with vertex colors
    std::map<std::pair<int, bool>, int> colored;
    auto materialFor = [&](const cgltf_material *gm, bool vcolors) {
        int base = gm && materials.count(gm) ? materials[gm] : -1;
        if (!vcolors) {
            return base;
        }
        auto key = std::make_pair(base, vcolors);
        auto it = colored.find(key);
        if (it != colored.end()) {
            return it->second;
        }
        Material m = base >= 0 ? model->materials[base] : Material();
        m.vertexColors = true;
        if (base < 0) {
            m.diffuse = {1, 1, 1, 1};
        }
        int index = (int)model->materials.size();
        model->materials.push_back(m);
        return colored[key] = index;
    };

    // the meshes: one per primitive
    std::map<const cgltf_mesh *, std::vector<int>> meshes;
    for (size_t mi = 0; mi < data->meshes_count; mi++) {
        const cgltf_mesh &gm = data->meshes[mi];
        for (size_t pi = 0; pi < gm.primitives_count; pi++) {
            const cgltf_primitive &prim = gm.primitives[pi];
            const cgltf_accessor *pos = nullptr;
            const cgltf_accessor *nrm = nullptr;
            const cgltf_accessor *uv = nullptr;
            const cgltf_accessor *col = nullptr;
            for (size_t a = 0; a < prim.attributes_count; a++) {
                const cgltf_attribute &at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) {
                    pos = at.data;
                } else if (at.type == cgltf_attribute_type_normal) {
                    nrm = at.data;
                } else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) {
                    uv = at.data;
                } else if (at.type == cgltf_attribute_type_color && at.index == 0) {
                    col = at.data;
                }
            }
            if (!pos) {
                continue;
            }
            Mesh m;
            m.name = gm.name ? gm.name : "mesh " + std::to_string(mi);
            m.vertices.resize(pos->count);
            for (size_t v = 0; v < pos->count; v++) {
                float f[4] = {0, 0, 0, 1};
                cgltf_accessor_read_float(pos, v, f, 3);
                m.vertices[v].position = {f[0], f[1], f[2]};
                if (nrm && v < nrm->count && cgltf_accessor_read_float(nrm, v, f, 3)) {
                    m.vertices[v].normal = {f[0], f[1], f[2]};
                }
                if (uv && v < uv->count && cgltf_accessor_read_float(uv, v, f, 2)) {
                    m.vertices[v].uv = {f[0], 1 - f[1]}; // the rows of the images go up
                }
                if (col && v < col->count) {
                    int n = cgltf_num_components(col->type);
                    f[3] = 1;
                    if (cgltf_accessor_read_float(col, v, f, n)) {
                        m.vertices[v].color = packColor(f, n);
                    }
                }
            }
            std::vector<uint32_t> idx;
            if (prim.indices) {
                idx.resize(prim.indices->count);
                for (size_t k = 0; k < idx.size(); k++) {
                    idx[k] = (uint32_t)cgltf_accessor_read_index(prim.indices, k);
                }
            } else {
                idx.resize(pos->count);
                for (size_t k = 0; k < idx.size(); k++) {
                    idx[k] = (uint32_t)k;
                }
            }
            // the strips, fans and loops as lists
            switch (prim.type) {
            case cgltf_primitive_type_triangles:
                m.indices = idx;
                break;
            case cgltf_primitive_type_triangle_strip:
                for (size_t k = 0; k + 2 < idx.size(); k++) {
                    if (k % 2 == 0) {
                        m.indices.insert(m.indices.end(), {idx[k], idx[k + 1], idx[k + 2]});
                    } else {
                        m.indices.insert(m.indices.end(), {idx[k + 1], idx[k], idx[k + 2]});
                    }
                }
                break;
            case cgltf_primitive_type_triangle_fan:
                for (size_t k = 1; k + 1 < idx.size(); k++) {
                    m.indices.insert(m.indices.end(), {idx[0], idx[k], idx[k + 1]});
                }
                break;
            case cgltf_primitive_type_lines:
                m.primitive = Primitive::Lines;
                m.indices = idx;
                break;
            case cgltf_primitive_type_line_strip:
            case cgltf_primitive_type_line_loop:
                m.primitive = Primitive::Lines;
                for (size_t k = 0; k + 1 < idx.size(); k++) {
                    m.indices.insert(m.indices.end(), {idx[k], idx[k + 1]});
                }
                if (prim.type == cgltf_primitive_type_line_loop && idx.size() > 2) {
                    m.indices.insert(m.indices.end(), {idx.back(), idx[0]});
                }
                break;
            case cgltf_primitive_type_points:
                m.primitive = Primitive::Points;
                m.indices = idx;
                break;
            default:
                break;
            }
            if (m.indices.empty()) {
                continue;
            }
            if (!nrm && m.primitive == Primitive::Triangles) {
                m.computeNormals();
            }
            m.material = materialFor(prim.material, col != nullptr);
            m.computeBounds();
            meshes[&gm].push_back((int)model->meshes.size());
            model->meshes.push_back(std::move(m));
        }
    }

    // the nodes: node 0 the root (Y-up turned to Z-up), glTF node i at 1 + i
    model->nodes.resize(1 + data->nodes_count);
    model->nodes[0].name = path;
    model->nodes[0].local = axes::fromYUpMatrix();
    for (size_t i = 0; i < data->nodes_count; i++) {
        const cgltf_node &gn = data->nodes[i];
        Node &n = model->nodes[1 + i];
        n.name = gn.name ? gn.name : "";
        cgltf_node_transform_local(&gn, &n.local[0][0]);
        if (gn.mesh && meshes.count(gn.mesh)) {
            n.meshes = meshes[gn.mesh];
        }
        for (size_t c = 0; c < gn.children_count; c++) {
            n.children.push_back(1 + (int)(gn.children[c] - data->nodes));
        }
    }
    const cgltf_scene *scene = data->scene ? data->scene : data->scenes_count ? &data->scenes[0] : nullptr;
    if (scene) {
        for (size_t k = 0; k < scene->nodes_count; k++) {
            model->nodes[0].children.push_back(1 + (int)(scene->nodes[k] - data->nodes));
        }
    } else {
        // no scene: the nodes without parent
        for (size_t i = 0; i < data->nodes_count; i++) {
            if (!data->nodes[i].parent) {
                model->nodes[0].children.push_back(1 + (int)i);
            }
        }
    }
    cgltf_free(data);
    if (model->meshes.empty()) {
        if (error) {
            *error = path + ": nothing to draw";
        }
        return nullptr;
    }
    return model;
}

} // namespace r4::assets
