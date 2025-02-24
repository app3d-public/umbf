#pragma once

#include <astl/hash.hpp>
#include <core/math.hpp>
#include <core/meta.hpp>
#include <filesystem>
#include <finders_interface.h>
#include <vulkan/vulkan.hpp>

namespace assets
{
    // The file signature of the asset file format
    constexpr u32 sign_format_assets = 0xFFBECFB8;

    /**
     * @enum Type
     * @brief Enumerates the types of assets that can be managed.
     */
    enum class Type : u8
    {
        /// Undefined or  invalid
        Invalid,
        /// Texture image
        Image,
        /// Scene file
        Scene,
        /// Material
        Material,
        /**
         * Target to asset
         * It can be stored in a local repository or in the cloud
         */
        Target,
        /**
         * Asset library
         * It also can stored targets or embedded resources
         **/
        Library
    };

    // Get the string representation of the asset type
    APPLIB_API std::string toString(Type type);

    /**
     * @brief Represents a generic asset in the system.
     *
     * The Asset structure holds common data for different asset types.
     * The first block in the `blocks` array always corresponds to the type of the asset (e.g., Image, Material),
     * and the remaining blocks contain additional metadata related to the asset.
     */
    struct Asset
    {
        /**
         * @brief Header information for the asset.
         *
         * The header contains basic information about the asset, including its type
         * and whether it is compressed.
         */
        struct Header
        {
            Type type = Type::Invalid; //< The type of the asset
            bool compressed;           //< Indicates whether the asset data is compressed.
        } header;
        // Array of blocks, where the first block defines the asset type and the rest are metadata.
        astl::vector<astl::shared_ptr<meta::Block>> blocks;
        u32 checksum; //< Checksum of the asset for integrity validation.

        /**
         * @brief Reads and creates an asset from a binary stream.
         * @param path The path to the asset.
         * @return A shared pointer to the created asset.
         **/
        static APPLIB_API astl::shared_ptr<Asset> readFromFile(const std::filesystem::path &path);

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
        APPLIB_API bool save(const std::filesystem::path &path, int compression = 5);
    };

    namespace sign_block
    {
        constexpr u32 image2D = SIGN_APP_PART_DEFAULT << 16 | 0xAB6D;
        constexpr u32 image_atlas = SIGN_APP_PART_DEFAULT << 16 | 0x0DF3;
        constexpr u32 material = SIGN_APP_PART_DEFAULT << 16 | 0xFD5F;
        constexpr u32 scene = SIGN_APP_PART_DEFAULT << 16 | 0xA9FD;
        constexpr u32 mesh = SIGN_APP_PART_DEFAULT << 16 | 0x57CC;
        constexpr u32 material_range_assign = SIGN_APP_PART_DEFAULT << 16 | 0x3627;
        constexpr u32 material_info = SIGN_APP_PART_DEFAULT << 16 | 0x26EB;
        constexpr u32 target = SIGN_APP_PART_DEFAULT << 16 | 0x6B0C;
        constexpr u32 library = SIGN_APP_PART_DEFAULT << 16 | 0xB6AB;
    } // namespace sign_block

    // Represents a 2D image asset block.
    struct Image2D : meta::Block
    {
    public:
        u16 width;                              //< Width of the image in pixels.
        u16 height;                             //< Height of the image in pixels.
        int channelCount;                       //< Number of color channels in the image.
        astl::vector<std::string> channelNames; //< Names of the channels (e.g., "Red", "Green", "Blue").
        u16 bytesPerChannel;                    //< Number of bytes per channel.
        void *pixels;                           //< Pointer to the raw pixel data.
        vk::Format imageFormat;                 //<  Vulkan format of the image data.

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
        virtual const u32 signature() const override { return sign_block::image2D; }
    };

    /**
     * @brief Represents an atlas image block, derived from `Image2D`.
     *
     * The `Atlas` structure extends `Image2D` to support texture atlases. It includes
     * additional fields to manage atlas-specific data such as discarded steps, images
     * within the atlas, and packing data.
     */
    struct Atlas final : Image2D
    {
        using Spaces = rectpack2D::empty_spaces<false, rectpack2D::default_empty_spaces>;
        using Rect = rectpack2D::output_rect_t<Spaces>;

        u16 discardStep;                                //< Step used for discarding textures in the atlas.
        astl::vector<astl::shared_ptr<Image2D>> images; //< Collection of images contained in the atlas
        std::vector<Rect> packData;                     //< Data about the placement of images within the atlas.
        i16 padding;                                    //< Padding between images in the atlas.

        /**
         * @brief Returns the signature of the atlas block.
         *
         * Provides a unique signature for the `Atlas` block type to distinguish it
         * from other types of asset blocks, including `Image2D`.
         *
         * @return The signature of the block.
         */
        virtual const u32 signature() const override { return sign_block::image_atlas; }

        Atlas() = default;

        Atlas(const Image2D &image) : Image2D(image), discardStep(0) {}
    };

    /// @brief Pack atlas
    /// @return Returns true on success otherwise returns false
    APPLIB_API bool packAtlas(size_t maxSize, int discardStep, rectpack2D::flipping_option flip,
                              std::vector<Atlas::Rect> &dst);

    // Represents a node of material properties.
    struct MaterialNode
    {
        glm::vec3 rgb; //< RGB color value for the material.
        bool textured; //< Flag indicating if the material is textured.
        u16 textureID; //< Index of the texture. Defaults to -1 indicating no texture.
    };

    /**
     * @brief Represents a material asset block.
     *
     * The `Material` structure is used to store information about a material, including
     * references to texture assets and specific material properties such as albedo.
     * It inherits from `meta::Block` to provide a base for different types of asset blocks.
     */
    struct Material final : meta::Block
    {
        astl::vector<Asset> textures; //< Array of texture assets associated with the material.
        MaterialNode albedo;          //< Albedo property of the material, defining its base color.

        /**
         * @brief Returns the signature of the material block.
         *
         * Provides a unique signature for the `Material` block type to distinguish it
         * from other types of asset blocks.
         *
         * @return The signature of the block.
         */
        virtual const u32 signature() const override { return sign_block::material; }
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
        u64 id;                                           //< Unique identifier for the object.
        std::string name;                                 //< The name of the object.
        astl::vector<astl::shared_ptr<meta::Block>> meta; //< Metadata associated with the object.
    };

    /**
     * @brief Represents a scene containing multiple objects, textures, and materials.
     *
     * The `Scene` structure holds a collection of objects, textures, and materials that
     * make up a scene. It inherits from `meta::Block` to integrate with the asset system.
     */
    struct Scene final : meta::Block
    {
        astl::vector<Object> objects;  //< Array of objects present in the scene.
        astl::vector<Asset> textures;  //< Array of texture assets used in the scene.
        astl::vector<Asset> materials; //< Array of material assets used in the scene.

        /**
         * @brief Returns the signature of the scene block.
         *
         * Provides a unique signature for the `Scene` block type to distinguish it
         * from other types of asset blocks.
         *
         * @return The signature of the block.
         */
        virtual const u32 signature() const override { return sign_block::scene; }
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
            astl::vector<u32> vertices; ///< List of indices pointing to vertices in this group.
            astl::vector<u32> faces;    ///< List of indices of faces that reference these vertices.
        };

        // Represents a polygon face.
        template <typename Ref>
        struct Face
        {
            astl::vector<Ref> vertices; ///< List of vertex references that define the face.
            glm::vec3 normal;           ///< The normal vector of the face

            Face(const astl::vector<Ref> &verts = {}, const glm::vec3 &norm = {}) : vertices(verts), normal(norm) {}
            Face(std::initializer_list<Ref> verts, const glm::vec3 &norm = {}) : vertices(verts), normal(norm) {}
        };

        struct IndexedFace : Face<VertexRef>
        {
            u32 startID;    ///< Starting index in the index buffer for this face.
            u16 indexCount; ///< Number of indices that define this face.

            IndexedFace(const astl::vector<VertexRef> &vertices = {}, const glm::vec3 &norm = {0.0f, 0.0f, 0.0f},
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
            astl::vector<Vertex> vertices;   ///< Array containing all vertices of the model.
            u32 group_count;                 ///< Size of the vertex group array.
            astl::vector<IndexedFace> faces; ///< Array of faces that make up the model.
            astl::vector<u32> indices;       ///< Array of indices for rendering the model.
            AABB aabb;               ///< Axis-aligned bounding box that encloses the model.
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
        struct MeshBlock : meta::Block
        {
            Model model;                             ///< The 3D model data contained in this mesh block.
            astl::vector<bary::Vertex> baryVertices; ///< Array of vertices with barycentric coordinates.
            Transform transform;                     ///< Transformation information for the mesh.
            f32 normalsAngle = 0.0f;                 ///< 0: hard normals, otherwise soft normals using specified angle

            /**
             * @brief Returns the signature of the block.
             * @return The signature of the block.
             */
            virtual const u32 signature() const { return sign_block::mesh; }
        };
    } // namespace mesh

    // Represents material information as an asset block.
    struct MaterialInfo final : meta::Block
    {
        u64 id;
        std::string name;              //< The name of the material.
        astl::vector<u64> assignments; //< List of assignments objects IDs related to the material.

        MaterialInfo(u64 id = 0, const std::string &name = "", astl::vector<u64> assignments = {})
            : id(id), name(name), assignments(assignments)
        {
        }

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
        virtual const u32 signature() const { return sign_block::material_info; }
    };

    /**
     * @brief Represents material range assignment attributes as an asset block.
     *
     * The `MatRangeAssignAtrr` structure holds information about the assignment of materials
     * to specific ranges of faces. This can be used to specify different materials for different
     * parts of a model or mesh.
     */
    struct MatRangeAssignAtrr : meta::Block
    {
        u64 matID;               //< The ID of the material being assigned.
        astl::vector<u32> faces; //< Array of face indices to which the material is assigned.

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
        virtual const u32 signature() const { return sign_block::material_range_assign; }
    };

    /**
     * Structure representing a Target asset.
     * The Target class represents a type of asset that doesn't store the resource itself but only
     * information about where the resource is located and its location in the cache.
     */
    struct Target final : meta::Block
    {
        // Structure representing information about a target asset.
        struct Addr
        {
            // Enumeration representing the protocol for the target asset.
            enum class Proto
            {
                Unknown, // Unknown or unspecified protocol.
                File,    // Local repository
                Network  // Cloud repository
            } proto;
            std::string url; // URL or path to the target resource.
        } addr;
        Asset::Header header; // Asset header for dst resource
        u32 checksum;         // Checksum of the target resource.

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
        virtual const u32 signature() const { return sign_block::target; }
    };

    // The Library class serves as a storage for other assets. These assets can either be embedded or act as
    // targets.
    struct APPLIB_API Library final : meta::Block
    {
        // Represents a node in the file structure of the asset library.
        struct Node
        {
            std::string name;            // Name of the file node.
            astl::vector<Node> children; // Child nodes of this file node.
            bool isFolder{false};        // Flag indicating if the node is a folder.
            Asset asset;                 // The asset associated with the node.
        } fileTree;

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
        virtual const u32 signature() const { return sign_block::library; }

        /**
         * @brief Retrieves a list of assets associated with a specified path.
         * @param path Filesystem path to query for assets.
         * @return File search result
         */
        Node *getNode(const std::filesystem::path &path);
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
        using iterator = astl::hashmap<std::string, astl::shared_ptr<Library>>::iterator;
        using const_iterator = astl::hashmap<std::string, astl::shared_ptr<Library>>::const_iterator;

        /**
         * @brief Get the asset libraries size
         */
        size_t size() const { return _libraries.size(); }

        astl::shared_ptr<Library> operator[](const std::string &name)
        {
            auto it = _libraries.find(name);
            if (it == _libraries.end()) return nullptr;
            return it->second;
        }

        astl::shared_ptr<Library> operator[](const std::string &name) const
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

        void init(const std::filesystem::path &assetsPath);

    private:
        astl::hashmap<std::string, astl::shared_ptr<Library>> _libraries;
    };

    namespace streams
    {
        APPLIB_API void writeImage2D(astl::bin_stream &stream, meta::Block *block);
        APPLIB_API meta::Block *readImage2D(astl::bin_stream &stream);
        inline meta::Stream image2D = {readImage2D, writeImage2D};

        APPLIB_API meta::Block *readImageAtlas(astl::bin_stream &stream);
        APPLIB_API void writeImageAtlas(astl::bin_stream &stream, meta::Block *block);
        inline meta::Stream image_atlas = {readImageAtlas, writeImageAtlas};

        APPLIB_API meta::Block *readMaterial(astl::bin_stream &stream);
        APPLIB_API void writeMaterial(astl::bin_stream &stream, meta::Block *block);
        inline meta::Stream material = {readMaterial, writeMaterial};

        APPLIB_API meta::Block *readMaterialInfo(astl::bin_stream &stream);
        inline void writeMaterialInfo(astl::bin_stream &stream, meta::Block *block)
        {
            MaterialInfo *material = static_cast<MaterialInfo *>(block);
            stream.write(material->name)
                .write(static_cast<u32>(material->assignments.size()))
                .write(material->assignments.data(), material->assignments.size());
        }
        inline meta::Stream material_info = {readMaterialInfo, writeMaterialInfo};

        APPLIB_API meta::Block *readMatRangeAssign(astl::bin_stream &stream);
        APPLIB_API void writeMatRangeAssign(astl::bin_stream &stream, meta::Block *block);
        inline meta::Stream mat_range_assign = {readMatRangeAssign, writeMatRangeAssign};

        APPLIB_API meta::Block *readScene(astl::bin_stream &stream);
        APPLIB_API void writeScene(astl::bin_stream &stream, meta::Block *block);
        inline meta::Stream scene = {readScene, writeScene};

        APPLIB_API meta::Block *readMesh(astl::bin_stream &stream);
        APPLIB_API void writeMesh(astl::bin_stream &stream, meta::Block *block);
        inline meta::Stream mesh = {readMesh, writeMesh};

        APPLIB_API meta::Block *readTarget(astl::bin_stream &stream);
        APPLIB_API void writeTarget(astl::bin_stream &stream, meta::Block *block);
        inline meta::Stream target = {readTarget, writeTarget};

        APPLIB_API meta::Block *readLibrary(astl::bin_stream &stream);
        APPLIB_API void writeLibrary(astl::bin_stream &stream, meta::Block *block);
        inline meta::Stream library = {readLibrary, writeLibrary};
    } // namespace streams
} // namespace assets

namespace astl
{
    template <>
    inline bin_stream &bin_stream::write(const assets::Asset::Header &src)
    {
        u8 data = (static_cast<u8>(src.type) & 0x3F) | (static_cast<u8>(src.compressed) << 6);
        return write(data);
    }

    template <>
    bin_stream &bin_stream::read(assets::Asset::Header &dst);

    template <>
    APPLIB_API bin_stream &bin_stream::write(const astl::vector<astl::shared_ptr<meta::Block>> &meta);

    template <>
    APPLIB_API bin_stream &bin_stream::read(astl::vector<astl::shared_ptr<meta::Block>> &meta);

    template <>
    inline bin_stream &bin_stream::write(const assets::Asset &asset)
    {
        return write(asset.header).write(asset.blocks);
    }

    template <>
    inline bin_stream &bin_stream::read(assets::Asset &dst)
    {
        return read(dst.header).read(dst.blocks);
    }

    template <>
    inline bin_stream &bin_stream::write(const astl::vector<assets::Asset> &assets)
    {
        write(static_cast<u16>(assets.size()));
        for (auto &asset : assets) write(asset);
        return *this;
    }

    template <>
    bin_stream &bin_stream::read(astl::vector<assets::Asset> &dst);

    template <>
    inline bin_stream &bin_stream::write(const assets::MaterialNode &src)
    {
        u16 data = src.textured ? ((1 << 15) | (src.textureID & 0x7FFF)) : 0;
        return write(src.rgb).write(data);
    }

    template <>
    bin_stream &bin_stream::read(assets::MaterialNode &dst);

    template <>
    bin_stream &bin_stream::write(const assets::Library::Node &node);

    template <>
    bin_stream &bin_stream::read(assets::Library::Node &node);
} // namespace astl

namespace std
{
    template <>
    struct hash<assets::mesh::Vertex>
    {
        size_t operator()(const assets::mesh::Vertex &vertex) const
        {
            size_t seed = 0;
            hash_combine(seed, vertex.pos);
            hash_combine(seed, vertex.uv);
            hash_combine(seed, vertex.normal);
            return seed;
        }
    };
} // namespace std