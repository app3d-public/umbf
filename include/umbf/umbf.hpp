#pragma once

#include <acul/hash/hashmap.hpp>
#include <acul/io/path.hpp>
#include <acul/math.hpp>
#include <acul/meta.hpp>
#include <acul/string/string.hpp>
#include <finders_interface.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#define UMBF_MAGIC     0xCA9FB393
#define UMBF_VENDOR_ID 0xBC037D

namespace umbf
{
    /**
     * @brief Represents a generic asset in the system.
     *
     * The Asset structure holds common data for different asset types.
     * The first block in the `blocks` array always corresponds to the type of the asset (e.g., Image, Material),
     * and the remaining blocks contain additional metadata related to the asset.
     */
    struct File
    {
        struct Header
        {
            struct Pack;
            u32 vendor_sign;
            u32 vendor_version;
            u16 type_sign;
            u32 spec_version;
            bool compressed;
        } header;
        // Array of blocks, where the first block defines the asset type and the rest are metadata.
        acul::vector<acul::shared_ptr<acul::meta::block>> blocks;
        u32 checksum; //< Checksum of the asset for integrity validation.

        /**
         * @brief Reads and creates an asset from a binary stream.
         * @param path The path to the asset.
         * @return A shared pointer to the created asset.
         **/
        static APPLIB_API acul::shared_ptr<File> readFromDisk(const acul::string &path);

        /**
         * @brief Saves the asset to a file.
         *
         * This function saves the asset to the specified file path. It optionally
         * compresses the asset data with a given compression level.
         *
         * @param path The path to save the asset file.
         * @param compression The level of compression to apply (default is 5).
         * @return True if the asset was saved successfully, false otherwise.
         */
        APPLIB_API bool save(const acul::string &path, int compression = 5);
    };

    struct File::Header::Pack
    {
        u32 vendor_sign : 24;
        u32 compressed : 8;
        u32 vendor_version : 24;
        u32 type_sign_low : 8;
        u32 type_sign_high : 8;
        u32 spec_version : 24;
    };

    APPLIB_API void packHeader(const File::Header &src, File::Header::Pack &dst);
    APPLIB_API void unpackHeader(const File::Header::Pack &src, File::Header &dst);

    namespace sign_block
    {
        namespace format
        {
            enum : u16
            {
                none = 0x0,
                image = 0x0490,
                scene = 0xD20C,
                material = 0x78DB,
                target = 0x613E,
                library = 0x1A2C,
                raw = 0x4D4D
            };
        }

        namespace meta
        {
            enum : u32
            {
                none = 0x0,
                image2D = 0x7684573F,
                image_atlas = 0xA3903A92,
                material = 0xA8D0C51E,
                scene = 0xB7A3EE80,
                mesh = 0xF224B521,
                material_range_assign = 0xC441E54D,
                material_info = 0x6112A229,
                target = 0x0491F4E9,
                library = 0x8D7824FA
            };
        }
    } // namespace sign_block

    // Represents a 2D image asset block.
    struct Image2D : acul::meta::block
    {
    public:
        u16 width;                               //< Width of the image in pixels.
        u16 height;                              //< Height of the image in pixels.
        int channelCount;                        //< Number of color channels in the image.
        acul::vector<acul::string> channelNames; //< Names of the channels (e.g., "Red", "Green", "Blue").
        u16 bytesPerChannel;                     //< Number of bytes per channel.
        void *pixels;                            //< Pointer to the raw pixel data.
        vk::Format imageFormat;                  //<  Vulkan format of the image data.

        /**
         * @brief Calculates the size of the image in bytes.
         *
         * This function computes the total size of the image based on its dimensions,
         * channel count, and bytes per channel.
         *
         * @return The total size of the image in bytes.
         */
        vk::DeviceSize imageSize() const { return width * height * bytesPerChannel * channelCount; }

        /**
         * @brief Returns the signature of the block.
         *
         * Provides a unique signature for the `Image2D` block type to distinguish it
         * from other types of asset blocks.
         *
         * @return The signature of the block.
         */
        virtual u32 signature() const override { return sign_block::meta::image2D; }
    };

    /**
     * @brief Represents an atlas image block, derived from `Image2D`.
     *
     * The `Atlas` structure extends `Image2D` to support texture atlases. It includes
     * additional fields to manage atlas-specific data such as discarded steps, images
     * within the atlas, and packing data.
     */
    struct Atlas : acul::meta::block
    {
        using Spaces = rectpack2D::empty_spaces<false, rectpack2D::default_empty_spaces>;
        using Rect = rectpack2D::output_rect_t<Spaces>;

        u16 discardStep;            //< Step used for discarding textures in the atlas.
        std::vector<Rect> packData; //< Data about the placement of images within the atlas.
        i16 padding;                //< Padding between images in the atlas.

        /**
         * @brief Returns the signature of the atlas block.
         *
         * Provides a unique signature for the `Atlas` block type to distinguish it
         * from other types of asset blocks, including `Image2D`.
         *
         * @return The signature of the block.
         */
        virtual u32 signature() const override { return sign_block::meta::image_atlas; }

        Atlas() : discardStep(0) {}
    };

    /// @brief Pack atlas
    /// @return Returns true on success otherwise returns false
    APPLIB_API bool packAtlas(size_t maxSize, int discardStep, rectpack2D::flipping_option flip,
                              std::vector<Atlas::Rect> &dst);

    /// @brief Fill image pixels data after packing atlas
    /// @param image Image block
    /// @param atlas Atlas block
    /// @param src Source images
    APPLIB_API void fillAtlasPixels(const acul::shared_ptr<Image2D> &image, const acul::shared_ptr<Atlas> &atlas,
                                    const acul::vector<acul::shared_ptr<Image2D>> &src);

    // Represents a node of material properties.
    struct MaterialNode
    {
        glm::vec3 rgb;  //< RGB color value for the material.
        bool textured;  //< Flag indicating if the material is textured.
        i16 texture_id; //< Index of the texture. Defaults to -1 indicating no texture.
    };

    /**
     * @brief Represents a material asset block.
     *
     * The `Material` structure is used to store information about a material, including
     * references to texture assets and specific material properties such as albedo.
     * It inherits from `acul::meta::block` to provide a base for different types of asset blocks.
     */
    struct Material final : acul::meta::block
    {
        acul::vector<File> textures; //< Array of texture assets associated with the material.
        MaterialNode albedo;         //< Albedo property of the material, defining its base color.

        /**
         * @brief Returns the signature of the material block.
         *
         * Provides a unique signature for the `Material` block type to distinguish it
         * from other types of asset blocks.
         *
         * @return The signature of the block.
         */
        virtual u32 signature() const override { return sign_block::meta::material; }
    };

    /**
     * @brief Represents a generic object within a scene.
     *
     * The `Object` structure contains basic information about an object, such as its name
     * and transformation data (position, rotation, and scale). It also holds a collection
     * of metadata blocks related to the object.
     */
    struct Object
    {
        u64 id;                                                 //< Unique identifier for the object.
        acul::string name;                                      //< The name of the object.
        acul::vector<acul::shared_ptr<acul::meta::block>> meta; //< Metadata associated with the object.
    };

    /**
     * @brief Represents a scene containing multiple objects, textures, and materials.
     *
     * The `Scene` structure holds a collection of objects, textures, and materials that
     * make up a scene. It inherits from `acul::meta::block` to integrate with the asset system.
     */
    struct Scene final : acul::meta::block
    {
        acul::vector<Object> objects; //< Array of objects present in the scene.
        acul::vector<File> textures;  //< Array of texture assets used in the scene.
        acul::vector<File> materials; //< Array of material assets used in the scene.

        /**
         * @brief Returns the signature of the scene block.
         *
         * Provides a unique signature for the `Scene` block type to distinguish it
         * from other types of asset blocks.
         *
         * @return The signature of the block.
         */
        virtual u32 signature() const override { return sign_block::meta::scene; }
    };

    namespace mesh
    {
        /// Representation of a unique vertex per vertex attributes - Position, UV coordinates, Normals, etc.
        /// Indexed structure for rendering purpose
        struct Vertex
        {
            /// @brief Position
            glm::vec3 pos{0.0f, 0.0f, 0.0f};

            /// @brief UV coordinates
            glm::vec2 uv{0.0f, 0.0f};

            /// @brief Normal
            glm::vec3 normal{0.0f, 0.0f, 0.0f};

            bool operator==(const Vertex &other) const
            {
                return pos == other.pos && uv == other.uv && normal == other.normal;
            }
        };

        /**
         * @brief Represents a reference to a vertex within a group. The group index refers to a vertex group,
         * and the vertex index refers to a specific vertex within that group.
         */
        struct VertexRef
        {
            u32 group;  ///< Index of the vertex group.
            u32 vertex; ///< Index of the specific vertex within the entire vertex buffer
        };

        // Represents a group of vertices.
        struct VertexGroup
        {
            acul::vector<u32> vertices; ///< List of indices pointing to vertices in this group.
            acul::vector<u32> faces;    ///< List of indices of faces that reference these vertices.
        };

        // Represents a polygon face.
        template <typename Ref>
        struct Face
        {
            acul::vector<Ref> vertices; ///< List of vertex references that define the face.
            glm::vec3 normal;           ///< The normal vector of the face

            Face(const acul::vector<Ref> &verts = {}, const glm::vec3 &norm = {}) : vertices(verts), normal(norm) {}
            Face(std::initializer_list<Ref> verts, const glm::vec3 &norm = {}) : vertices(verts), normal(norm) {}
        };

        struct IndexedFace : Face<VertexRef>
        {
            u32 startID;    ///< Starting index in the index buffer for this face.
            u16 indexCount; ///< Number of indices that define this face.

            IndexedFace(const acul::vector<VertexRef> &vertices = {}, const glm::vec3 &norm = {0.0f, 0.0f, 0.0f},
                        u32 sid = 0, u16 icount = 0)
                : Face(vertices, norm), startID(sid), indexCount(icount)
            {
            }
            IndexedFace(std::initializer_list<VertexRef> verts, const glm::vec3 &norm, u32 sid, u16 icount)
                : Face<VertexRef>(verts, norm), startID(sid), indexCount(icount)
            {
            }
        };

        using AABB = math::min_max<glm::vec3>;

        // Represents a 3D mesh model.
        struct Model
        {
            acul::vector<Vertex> vertices;   ///< Array containing all vertices of the model.
            u32 group_count;                 ///< Size of the vertex group array.
            acul::vector<IndexedFace> faces; ///< Array of faces that make up the model.
            acul::vector<u32> indices;       ///< Array of indices for rendering the model.
            AABB aabb;                       ///< Axis-aligned bounding box that encloses the model.
        };

        namespace bary
        {
            // Represents a vertex with barycentric coordinates.
            struct Vertex
            {
                glm::vec3 pos{0.0f, 0.0f, 0.0f};         ///< The position of the vertex.
                glm::vec3 barycentric{0.0f, 0.0f, 0.0f}; ///< The barycentric coordinates of the vertex.

                bool operator==(const Vertex &other) const
                {
                    return pos == other.pos && barycentric == other.barycentric;
                }
            };
        }; // namespace bary

        struct Transform
        {
            glm::vec3 position = {0.0f, 0.0f, 0.0f};
            glm::vec3 rotation = {0.0f, 0.0f, 0.0f};
            glm::vec3 scale = {1.0f, 1.0f, 1.0f};
        };

        // Represents a block of mesh data.
        struct MeshBlock : acul::meta::block
        {
            Model model;                             ///< The 3D model data contained in this mesh block.
            acul::vector<bary::Vertex> baryVertices; ///< Array of vertices with barycentric coordinates.
            Transform transform;                     ///< Transformation information for the mesh.
            f32 normalsAngle = 0.0f;                 ///< 0: hard normals, otherwise soft normals using specified angle

            /**
             * @brief Returns the signature of the block.
             * @return The signature of the block.
             */
            virtual u32 signature() const override { return sign_block::meta::mesh; }
        };
    } // namespace mesh

    // Represents material information as an asset block.
    struct MaterialInfo final : acul::meta::block
    {
        u64 id;
        acul::string name;             //< The name of the material.
        acul::vector<u64> assignments; //< List of assignments objects IDs related to the material.

        MaterialInfo(u64 id = 0, const acul::string &name = "", acul::vector<u64> assignments = {})
            : id(id), name(name), assignments(assignments)
        {
        }

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
        virtual u32 signature() const override { return sign_block::meta::material_info; }
    };

    /**
     * @brief Represents material range assignment attributes as an asset block.
     *
     * The `MatRangeAssignAtrr` structure holds information about the assignment of materials
     * to specific ranges of faces. This can be used to specify different materials for different
     * parts of a model or mesh.
     */
    struct MatRangeAssignAtrr : acul::meta::block
    {
        u64 matID;               //< The ID of the material being assigned.
        acul::vector<u32> faces; //< Array of face indices to which the material is assigned.

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
        virtual u32 signature() const override { return sign_block::meta::material_range_assign; }
    };

    /**
     * Structure representing a Target asset.
     * The Target class represents a type of asset that doesn't store the resource itself but only
     * information about where the resource is located and its location in the cache.
     */
    struct Target final : acul::meta::block
    {
        acul::string url;    // URL of the target resource
        File::Header header; // Asset header for dst resource
        u32 checksum;        // Checksum of the target resource.

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
        virtual u32 signature() const override { return sign_block::meta::target; }
    };

    // The Library class serves as a storage for other assets. These assets can either be embedded or act as
    // targets.
    struct APPLIB_API Library final : acul::meta::block
    {
        // Represents a node in the file structure of the asset library.
        struct Node
        {
            acul::string name;           // Name of the file node.
            acul::vector<Node> children; // Child nodes of this file node.
            bool isFolder{false};        // Flag indicating if the node is a folder.
            File asset;                  // The asset associated with the node.
        } fileTree;

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
        virtual u32 signature() const override { return sign_block::meta::library; }

        /**
         * @brief Retrieves a list of assets associated with a specified path.
         * @param path Filesystem path to query for assets.
         * @return File search result
         */
        Node *getNode(const acul::io::path &path);
    };

    /**
     * Class responsible for managing asset libraries.
     *
     * The Manager class oversees and provides access to various asset libraries, allowing for efficient
     * organization and retrieval of libraries by their names. It provides iterator functionality for easy
     * traversal of the libraries and ensures streamlined integration with the underlying system through
     * its initialization method.
     */
    class APPLIB_API Registry
    {
    public:
        using iterator = acul::hashmap<acul::string, acul::shared_ptr<Library>>::iterator;
        using const_iterator = acul::hashmap<acul::string, acul::shared_ptr<Library>>::const_iterator;

        /**
         * @brief Get the asset libraries size
         */
        size_t size() const { return _libraries.size(); }

        acul::shared_ptr<Library> operator[](const acul::string &name)
        {
            auto it = _libraries.find(name);
            if (it == _libraries.end()) return nullptr;
            return it->second;
        }

        acul::shared_ptr<Library> operator[](const acul::string &name) const
        {
            auto it = _libraries.find(name);
            if (it == _libraries.end()) return nullptr;
            return it->second;
        }

        iterator begin() { return _libraries.begin(); }
        const_iterator begin() const { return _libraries.begin(); }
        const_iterator cbegin() const { return _libraries.cbegin(); }
        iterator end() { return _libraries.end(); }
        const_iterator end() const { return _libraries.end(); }
        const_iterator cend() const { return _libraries.cend(); }

        void init(const acul::io::path &assetsPath);

    private:
        acul::hashmap<acul::string, acul::shared_ptr<Library>> _libraries;
    };

    namespace streams
    {
        APPLIB_API void writeImage2D(acul::bin_stream &stream, acul::meta::block *block);
        APPLIB_API acul::meta::block *readImage2D(acul::bin_stream &stream);
        inline acul::meta::stream image2D = {readImage2D, writeImage2D};

        APPLIB_API acul::meta::block *readImageAtlas(acul::bin_stream &stream);
        APPLIB_API void writeImageAtlas(acul::bin_stream &stream, acul::meta::block *block);
        inline acul::meta::stream image_atlas = {readImageAtlas, writeImageAtlas};

        APPLIB_API acul::meta::block *readMaterial(acul::bin_stream &stream);
        APPLIB_API void writeMaterial(acul::bin_stream &stream, acul::meta::block *block);
        inline acul::meta::stream material = {readMaterial, writeMaterial};

        APPLIB_API acul::meta::block *readMaterialInfo(acul::bin_stream &stream);
        inline void writeMaterialInfo(acul::bin_stream &stream, acul::meta::block *block)
        {
            MaterialInfo *material = static_cast<MaterialInfo *>(block);
            stream.write(material->id)
                .write(material->name)
                .write(static_cast<u32>(material->assignments.size()))
                .write(material->assignments.data(), material->assignments.size());
        }
        inline acul::meta::stream material_info = {readMaterialInfo, writeMaterialInfo};

        APPLIB_API acul::meta::block *readMatRangeAssign(acul::bin_stream &stream);
        APPLIB_API void writeMatRangeAssign(acul::bin_stream &stream, acul::meta::block *block);
        inline acul::meta::stream mat_range_assign = {readMatRangeAssign, writeMatRangeAssign};

        APPLIB_API acul::meta::block *readScene(acul::bin_stream &stream);
        APPLIB_API void writeScene(acul::bin_stream &stream, acul::meta::block *block);
        inline acul::meta::stream scene = {readScene, writeScene};

        APPLIB_API acul::meta::block *readMesh(acul::bin_stream &stream);
        APPLIB_API void writeMesh(acul::bin_stream &stream, acul::meta::block *block);
        inline acul::meta::stream mesh = {readMesh, writeMesh};

        APPLIB_API acul::meta::block *readTarget(acul::bin_stream &stream);
        APPLIB_API void writeTarget(acul::bin_stream &stream, acul::meta::block *block);
        inline acul::meta::stream target = {readTarget, writeTarget};

        APPLIB_API acul::meta::block *readLibrary(acul::bin_stream &stream);
        APPLIB_API void writeLibrary(acul::bin_stream &stream, acul::meta::block *block);
        inline acul::meta::stream library = {readLibrary, writeLibrary};
    } // namespace streams
} // namespace umbf

namespace acul
{
    template <>
    APPLIB_API bin_stream &bin_stream::write(const acul::vector<acul::shared_ptr<acul::meta::block>> &meta);

    template <>
    APPLIB_API bin_stream &bin_stream::read(acul::vector<acul::shared_ptr<acul::meta::block>> &meta);

    template <>
    inline bin_stream &bin_stream::write(const umbf::File::Header &header)
    {
        umbf::File::Header::Pack pack;
        umbf::packHeader(header, pack);
        return write(pack);
    }

    template <>
    inline bin_stream &bin_stream::write(const umbf::File &file)
    {
        return write(file.header).write(file.blocks);
    }

    template <>
    inline bin_stream &bin_stream::read(umbf::File::Header &dst)
    {
        umbf::File::Header::Pack pack;
        read(pack);
        umbf::unpackHeader(pack, dst);
        return *this;
    }

    template <>
    inline bin_stream &bin_stream::read(umbf::File &dst)
    {
        return read(dst.header).read(dst.blocks);
    }

    template <>
    inline bin_stream &bin_stream::write(const acul::vector<umbf::File> &assets)
    {
        write(static_cast<u16>(assets.size()));
        for (auto &asset : assets) write(asset);
        return *this;
    }

    template <>
    bin_stream &bin_stream::read(acul::vector<umbf::File> &dst);

    template <>
    inline bin_stream &bin_stream::write(const umbf::MaterialNode &src)
    {
        u16 data = src.textured ? ((1 << 15) | (src.texture_id & 0x7FFF)) : 0;
        return write(src.rgb).write(data);
    }

    template <>
    bin_stream &bin_stream::read(umbf::MaterialNode &dst);

    template <>
    bin_stream &bin_stream::write(const umbf::Library::Node &node);

    template <>
    bin_stream &bin_stream::read(umbf::Library::Node &node);
} // namespace acul

namespace std
{
    template <>
    struct hash<umbf::mesh::Vertex>
    {
        size_t operator()(const umbf::mesh::Vertex &vertex) const
        {
            size_t seed = 0;
            hash_combine(seed, vertex.pos);
            hash_combine(seed, vertex.uv);
            hash_combine(seed, vertex.normal);
            return seed;
        }
    };
} // namespace std