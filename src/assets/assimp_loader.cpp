// src/assets/assimp_loader.cpp - the formats without a loader of R4nkine, read by Assimp (FBX, COLLADA, 3DS, LWO, PLY, STL, X, Blender...): only when R4nkine is built with it (R4_WITH_ASSIMP).
#include "r4/assets/model.h"
#include "r4/axes.h"

#ifdef R4_HAVE_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#endif

#include <algorithm>
#include <cstdio>
#include <map>

namespace r4::assets {

#ifdef R4_HAVE_ASSIMP

namespace {

Vec4 colorOf(const aiMaterial &am, const char *key, unsigned int type, unsigned int index, Vec4 fallback) {
    aiColor4D c;
    if (am.Get(key, type, index, c) != AI_SUCCESS) {
        return fallback;
    }
    return {c.r, c.g, c.b, c.a};
}

float floatOf(const aiMaterial &am, const char *key, unsigned int type, unsigned int index, float fallback) {
    float f = 0;
    if (am.Get(key, type, index, f) != AI_SUCCESS) {
        return fallback;
    }
    return f;
}

// an embedded texture: compressed (PNG, JPEG... when its height is 0) or ARGB8888 texels
bool embeddedImage(const aiTexture &t, Image &img, std::string *error) {
    if (t.mHeight == 0) {
        return decodeImage(reinterpret_cast<const uint8_t *>(t.pcData), t.mWidth, img, error);
    }
    img.width = (int)t.mWidth;
    img.height = (int)t.mHeight;
    img.channels = 4;
    img.pixels.resize((size_t)img.width * img.height * 4);
    for (size_t i = 0; i < (size_t)img.width * img.height; i++) {
        const aiTexel &x = t.pcData[i];
        img.pixels[i * 4] = x.r;
        img.pixels[i * 4 + 1] = x.g;
        img.pixels[i * 4 + 2] = x.b;
        img.pixels[i * 4 + 3] = x.a;
    }
    return true;
}

bool hasClearTexels(const Image &img) {
    if (img.channels != 4 && img.channels != 2) {
        return false;
    }
    for (size_t i = img.channels - 1; i < img.pixels.size(); i += img.channels) {
        if (img.pixels[i] < 128) {
            return true;
        }
    }
    return false;
}

Mat4 toMat4(const aiMatrix4x4 &m) {
    Mat4 r(1.0f);
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            r[col][row] = m[row][col];
        }
    }
    return r;
}

} // namespace

std::vector<std::string> assimpExtensions() {
    Assimp::Importer importer;
    std::string list;
    importer.GetExtensionList(list);
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < list.size()) {
        size_t end = list.find(';', pos);
        if (end == std::string::npos) {
            end = list.size();
        }
        std::string e = list.substr(pos, end - pos);
        if (e.rfind("*.", 0) == 0) {
            e = e.substr(2);
        }
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });
        if (!e.empty()) {
            out.push_back(e);
        }
        pos = end + 1;
    }
    return out;
}

std::shared_ptr<Model> loadAssimp(const std::string &path, std::string *error) {
    Assimp::Importer importer;
    const unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals | aiProcess_SortByPType | aiProcess_RemoveRedundantMaterials;
    const aiScene *scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        if (error) {
            *error = path + ": " + importer.GetErrorString();
        }
        return nullptr;
    }
    size_t slash = path.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    auto model = std::make_shared<Model>();
    model->source = path;

    std::map<std::string, int> images;
    auto imageOf = [&](const aiMaterial &am, aiTextureType type) -> int {
        aiString file;
        aiTextureMapMode wrap[2] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
        if (am.GetTextureCount(type) == 0 || am.GetTexture(type, 0, &file, nullptr, nullptr, nullptr, nullptr, wrap) != AI_SUCCESS) {
            return -1;
        }
        std::string name = file.C_Str();
        auto it = images.find(name);
        if (it != images.end()) {
            return it->second;
        }
        Image img;
        std::string err;
        bool ok = false;
        if (const aiTexture *t = scene->GetEmbeddedTexture(file.C_Str())) {
            ok = embeddedImage(*t, img, &err);
        } else {
            std::string found = findAsset(dir, name);
            ok = !found.empty() && loadImage(found, img, &err);
            if (found.empty()) {
                err = "not found";
            }
        }
        int index = -1;
        if (ok) {
            img.name = name;
            img.clampS = wrap[0] == aiTextureMapMode_Clamp;
            img.clampT = wrap[1] == aiTextureMapMode_Clamp;
            index = (int)model->images.size();
            model->images.push_back(std::move(img));
        } else {
            std::fprintf(stderr, "r4: assimp: cannot read the image %s of %s: %s\n", name.c_str(), path.c_str(), err.c_str());
        }
        images[name] = index;
        return index;
    };

    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        const aiMaterial &am = *scene->mMaterials[i];
        Material m;
        aiString name;
        m.name = am.Get(AI_MATKEY_NAME, name) == AI_SUCCESS ? name.C_Str() : "material " + std::to_string(i);
        Vec4 base = colorOf(am, AI_MATKEY_BASE_COLOR, {-1, -1, -1, -1});
        m.diffuse = base.x >= 0 ? base : colorOf(am, AI_MATKEY_COLOR_DIFFUSE, {0.8f, 0.8f, 0.8f, 1});
        m.diffuse.w = floatOf(am, AI_MATKEY_OPACITY, m.diffuse.w);
        Vec4 ambient = colorOf(am, AI_MATKEY_COLOR_AMBIENT, {0, 0, 0, 1});
        m.ambient = ambient.x + ambient.y + ambient.z > 0 ? Vec3(ambient) : Vec3(1.0f);
        m.specular = Vec3(colorOf(am, AI_MATKEY_COLOR_SPECULAR, {0, 0, 0, 1}));
        m.emissive = Vec3(colorOf(am, AI_MATKEY_COLOR_EMISSIVE, {0, 0, 0, 1}));
        // a shininess of 0: no highlight, as Assimp means it
        float shininess = floatOf(am, AI_MATKEY_SHININESS, 16);
        if (shininess <= 0) {
            m.specular = Vec3(0.0f);
        }
        m.shininess = std::clamp(shininess, 1.0f, 256.0f);
        int twoSided = 0;
        if (am.Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
            m.doubleSided = twoSided != 0;
        }
        int shading = 0;
        if (am.Get(AI_MATKEY_SHADING_MODEL, shading) == AI_SUCCESS && (shading == aiShadingMode_NoShading || shading == aiShadingMode_Unlit)) {
            m.shading = Shading::UNLIT;
        }
        m.diffuseImage = imageOf(am, aiTextureType_BASE_COLOR);
        if (m.diffuseImage < 0) {
            m.diffuseImage = imageOf(am, aiTextureType_DIFFUSE);
        }
        if (m.diffuse.w < 1) {
            m.alpha = AlphaMode::BLEND;
        } else if (m.diffuseImage >= 0 && hasClearTexels(model->images[m.diffuseImage])) {
            m.alpha = AlphaMode::MASK;
        }
        model->materials.push_back(m);
    }
    std::map<int, int> colored;
    auto withVertexColors = [&](int base) {
        auto it = colored.find(base);
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
        colored[base] = index;
        return index;
    };

    std::vector<int> meshIndex(scene->mNumMeshes, -1);
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        const aiMesh &am = *scene->mMeshes[i];
        Mesh m;
        m.name = am.mName.C_Str();
        if (am.mPrimitiveTypes & aiPrimitiveType_POINT) {
            m.primitive = Primitive::POINTS;
        } else if (am.mPrimitiveTypes & aiPrimitiveType_LINE) {
            m.primitive = Primitive::LINES;
        }
        m.vertices.resize(am.mNumVertices);
        for (unsigned int v = 0; v < am.mNumVertices; v++) {
            Vertex &out = m.vertices[v];
            out.position = {am.mVertices[v].x, am.mVertices[v].y, am.mVertices[v].z};
            if (am.HasNormals()) {
                out.normal = {am.mNormals[v].x, am.mNormals[v].y, am.mNormals[v].z};
            }
            if (am.HasTextureCoords(0)) {
                // Assimp gives the texture coordinates of OpenGL (v from the bottom) for all the formats, as R4nkine
                out.uv = {am.mTextureCoords[0][v].x, am.mTextureCoords[0][v].y};
            }
            if (am.HasVertexColors(0)) {
                const aiColor4D &c = am.mColors[0][v];
                auto byte = [](float f) {
                    return (uint32_t)(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
                };
                out.color = byte(c.r) | byte(c.g) << 8 | byte(c.b) << 16 | byte(c.a) << 24;
            }
        }
        for (unsigned int f = 0; f < am.mNumFaces; f++) {
            const aiFace &face = am.mFaces[f];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                m.indices.push_back(face.mIndices[k]);
            }
        }
        if (m.indices.empty()) {
            continue;
        }
        // some files (STL of Blender) give null normals
        bool nullNormal = !am.HasNormals();
        for (const Vertex &v : m.vertices) {
            nullNormal = nullNormal || dot(v.normal, v.normal) < 1e-12f;
        }
        if (nullNormal && m.primitive == Primitive::TRIANGLES) {
            m.computeNormals();
        }
        int material = am.mMaterialIndex < scene->mNumMaterials ? (int)am.mMaterialIndex : -1;
        m.material = am.HasVertexColors(0) ? withVertexColors(material) : material;
        m.computeBounds();
        meshIndex[i] = (int)model->meshes.size();
        model->meshes.push_back(std::move(m));
    }

    // the nodes depth first, the root turned from Y up to Z up as glTF
    model->nodes.push_back(Node());
    model->nodes[0].name = path;
    model->nodes[0].local = axes::fromYUpMatrix();
    struct Pending {
        const aiNode *node;
        int parent;
    };
    std::vector<Pending> stack{{scene->mRootNode, 0}};
    while (!stack.empty()) {
        Pending p = stack.back();
        stack.pop_back();
        Node n;
        n.name = p.node->mName.C_Str();
        n.local = toMat4(p.node->mTransformation);
        for (unsigned int k = 0; k < p.node->mNumMeshes; k++) {
            int mi = meshIndex[p.node->mMeshes[k]];
            if (mi >= 0) {
                n.meshes.push_back(mi);
            }
        }
        int index = (int)model->nodes.size();
        model->nodes.push_back(n);
        model->nodes[p.parent].children.push_back(index);
        for (unsigned int c = p.node->mNumChildren; c > 0; c--) {
            stack.push_back({p.node->mChildren[c - 1], index});
        }
    }
    if (model->meshes.empty()) {
        if (error) {
            *error = path + ": nothing to draw";
        }
        return nullptr;
    }
    return model;
}

#else

std::vector<std::string> assimpExtensions() {
    return {};
}

std::shared_ptr<Model> loadAssimp(const std::string &path, std::string *error) {
    if (error) {
        *error = "unknown model format (R4nkine built without Assimp): " + path;
    }
    return nullptr;
}

#endif

} // namespace r4::assets
