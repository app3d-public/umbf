#pragma once

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
        astl::vector<std::shared_ptr<meta::Block>> blocks;
        u32 checksum; //< Checksum of the asset for integrity validation.

        /**
         * @brief Reads and creates an asset from a binary stream.
         * @param path The path to the asset.
         * @return A shared pointer to the created asset.
         **/
        static APPLIB_API std::shared_ptr<Asset> readFromFile(const std::filesystem::path &path);

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
     * @brief Stream handler for reading and writing `Image2D` blocks.
     *
     * This class provides the functionality to read `Image2D` blocks from a binary stream
     * and write them back. It implements the `meta::Stream` interface to handle stream operations.
     */
    class APPLIB_API Image2DStream : public meta::Stream
    {
    public:
        /**
         * @brief Reads an `Image2D` block from a binary stream.
         *
         * This method reads data from the provided stream and constructs an `Image2D` object.
         *
         * @param stream The binary stream to read from.
         * @return A pointer to the created `Image2D` block.
         */
        virtual meta::Block *readFromStream(astl::bin_stream &stream) override;

        /**
         * @brief Writes an `Image2D` block to a binary stream.
         *
         * This method serializes the `Image2D` block into the provided binary stream.
         *
         * @param stream The binary stream to write to.
         * @param block The `Image2D` block to write.
         */
        virtual void writeToStream(astl::bin_stream &stream, meta::Block *block) override;
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

        u16 discardStep;                               //< Step used for discarding textures in the atlas.
        astl::vector<std::shared_ptr<Image2D>> images; //< Collection of images contained in the atlas
        std::vector<Rect> packData;                    //< Data about the placement of images within the atlas.

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

    /**
     * @brief Stream handler for reading and writing `Atlas` blocks.
     *
     * This class provides the functionality to read `Atlas` blocks from a binary stream
     * and write them back. It implements the `meta::Stream` interface to handle stream operations.
     */
    class APPLIB_API AtlasStream final : public meta::Stream
    {
    public:
        /**
         * @brief Reads an `Atlas` block from a binary stream.
         *
         * This method reads data from the provided stream and constructs an `Atlas` object.
         *
         * @param stream The binary stream to read from.
         * @return A pointer to the created `Atlas` block.
         */
        virtual meta::Block *readFromStream(astl::bin_stream &stream) override;

        /**
         * @brief Writes an `Atlas` block to a binary stream.
         *
         * This method serializes the `Atlas` block into the provided binary stream.
         *
         * @param stream The binary stream to write to.
         * @param block The `Atlas` block to write.
         */
        virtual void writeToStream(astl::bin_stream &stream, meta::Block *block) override;
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
     * @brief Stream handler for reading and writing `Material` blocks.
     *
     * This class provides the functionality to read `Material` blocks from a binary stream
     * and write them back. It implements the `meta::Stream` interface to handle stream operations
     * specific to material assets.
     */
    class APPLIB_API MaterialStream final : public meta::Stream
    {
    public:
        /**
         * @brief Reads a `Material` block from a binary stream.
         *
         * This method reads data from the provided stream and constructs a `Material` object.
         *
         * @param stream The binary stream to read from.
         * @return A pointer to the created `Material` block.
         */
        virtual meta::Block *readFromStream(astl::bin_stream &stream) override;

        /**
         * @brief Writes a `Material` block to a binary stream.
         *
         * This method serializes the `Material` block into the provided binary stream.
         *
         * @param stream The binary stream to write to.
         * @param block The `Material` block to write.
         */
        virtual void writeToStream(astl::bin_stream &stream, meta::Block *block) override;
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
        std::string name; //< The name of the object.

        /**
         * @brief Holds transformation data for the object.
         *
         * This nested structure contains position, rotation, and scale information for
         * transforming the object within the scene.
         */
        struct Transform
        {
            glm::vec3 position = glm::vec3(0.0f); //< Position of the object in the scene.
            glm::vec3 rotation = glm::vec3(0.0f); //< Rotation of the object in the scene.
            glm::vec3 scale = glm::vec3(1.0f);    //< Scale of the object in the scene.
        } transform;

        astl::vector<std::shared_ptr<meta::Block>> meta; //< Metadata associated with the object.
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

    /**
     * @brief Stream handler for reading and writing `Scene` blocks.
     *
     * This class provides the functionality to read `Scene` blocks from a binary stream
     * and write them back. It implements the `meta::Stream` interface to handle stream operations
     * specific to scene assets.
     */
    class APPLIB_API SceneStream final : public meta::Stream
    {
    public:
        /**
         * @brief Reads a `Scene` block from a binary stream.
         *
         * This method reads data from the provided stream and constructs a `Scene` object.
         *
         * @param stream The binary stream to read from.
         * @return A pointer to the created `Scene` block.
         */
        virtual meta::Block *readFromStream(astl::bin_stream &stream) override;

        /**
         * @brief Writes a `Scene` block to a binary stream.
         *
         * This method serializes the `Scene` block into the provided binary stream.
         *
         * @param stream The binary stream to write to.
         * @param block The `Scene` block to write.
         */
        virtual void writeToStream(astl::bin_stream &stream, meta::Block *block) override;
    };
    namespace mesh
    {
        /// Representation of a unique vertex per vertex attributes - Position, UV coordinates, Normals, etc.
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
        struct Face
        {
            astl::vector<VertexRef> vertices; ///< List of vertex references that define the face.
            glm::vec3 normal;                 ///< The normal vector of the face
            u32 startID;                      ///< Starting index in the index buffer for this face.
            u16 indexCount;                   ///< Number of indices that define this face.
        };

        // Represents an axis-aligned bounding box.
        struct AABB
        {
            alignas(16) glm::vec3 min = glm::vec3(std::numeric_limits<f32>::max());
            alignas(16) glm::vec3 max = glm::vec3(std::numeric_limits<f32>::lowest());
        };

        // Represents a 3D mesh model.
        struct Model
        {
            astl::vector<Vertex> vertices;    ///< Array containing all vertices of the model.
            astl::vector<VertexGroup> groups; ///< Array containing groups of vertices.
            astl::vector<Face> faces;         ///< Array of faces that make up the model.
            astl::vector<u32> indices;        ///< Array of indices for rendering the model.
            AABB aabb;                        ///< Axis-aligned bounding box that encloses the model.
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

        // Represents a block of mesh data.
        struct MeshBlock : meta::Block
        {
            Model model;                             ///< The 3D model data contained in this mesh block.
            astl::vector<bary::Vertex> baryVertices; ///< Array of vertices with barycentric coordinates.

            /**
             * @brief Returns the signature of the block.
             * @return The signature of the block.
             */
            virtual const u32 signature() const { return sign_block::mesh; }
        };
        // Stream class for reading and writing mesh data.
        class APPLIB_API MeshStream final : public meta::Stream
        {
        public:
            /**
             * @brief Reads a block from the binary stream.
             * @param stream The binary stream to read from.
             * @return A pointer to the read block.
             */
            virtual meta::Block *readFromStream(astl::bin_stream &stream) override;

            /**
             * @brief Writes a block to the binary stream.
             * @param stream The binary stream to write to.
             * @param block The block to write.
             */
            virtual void writeToStream(astl::bin_stream &stream, meta::Block *block) override;
        };
    } // namespace mesh

    // Represents material information as an asset block.
    struct MaterialInfo final : meta::Block
    {
        std::string name;              //< The name of the material.
        astl::vector<u32> assignments; //< List of assignments IDs related to the material.

        MaterialInfo(const std::string &name = "", astl::vector<u32> assignments = {})
            : name(name), assignments(assignments)
        {
        }

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
        virtual const u32 signature() const { return sign_block::material_info; }
    };

    /**
     * @brief Stream handler for reading and writing `MaterialInfo` blocks.
     *
     * This class provides the functionality to read `MaterialInfo` blocks from a binary stream
     * and write them back. It implements the `meta::Stream` interface to handle stream operations
     * specific to material information assets.
     */
    class APPLIB_API MaterialInfoStream final : public meta::Stream
    {
    public:
        /**
         * @brief Writes a block to the binary stream.
         * @param stream The binary stream to write to.
         * @param block The block to write.
         */
        virtual void writeToStream(astl::bin_stream &stream, meta::Block *block) override
        {
            MaterialInfo *material = static_cast<MaterialInfo *>(block);
            stream.write(material->name)
                .write(static_cast<u32>(material->assignments.size()))
                .write(material->assignments.data(), material->assignments.size());
        }

        /**
         * @brief Reads a block from the binary stream.
         * @param stream The binary stream to read from.
         * @return A pointer to the read block.
         */
        virtual meta::Block *readFromStream(astl::bin_stream &stream) override;
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
        u32 matID;               //< The ID of the material being assigned.
        astl::vector<u32> faces; //< Array of face indices to which the material is assigned.

        /**
         * @brief Returns the signature of the block.
         * @return The signature of the block.
         */
        virtual const u32 signature() const { return sign_block::material_range_assign; }
    };

    /**
     * @brief Stream handler for reading and writing `MatRangeAssignAtrr` blocks.
     *
     * This class provides the functionality to read `MatRangeAssignAtrr` blocks from a binary stream
     * and write them back. It implements the `meta::Stream` interface to handle stream operations
     * specific to material range assignments.
     */
    class APPLIB_API MatRangeAssignStream final : public meta::Stream
    {
    public:
        /**
         * @brief Writes a `MatRangeAssignAtrr` block to a binary stream.
         *
         * This method serializes the `MatRangeAssignAtrr` block, writing its material ID and face indices
         * to the provided binary stream.
         *
         * @param stream The binary stream to write to.
         * @param block The `MatRangeAssignAtrr` block to write.
         */
        virtual void writeToStream(astl::bin_stream &stream, meta::Block *block) override;

        /**
         * @brief Reads a `MatRangeAssignAtrr` block from a binary stream.
         *
         * This method reads data from the provided stream and constructs a `MatRangeAssignAtrr` object.
         *
         * @param stream The binary stream to read from.
         * @return A pointer to the created `MatRangeAssignAtrr` block.
         */
        virtual meta::Block *readFromStream(astl::bin_stream &stream) override;
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

    /**
     * @brief Stream handler for reading and writing `Target` blocks.
     *
     * This class provides the functionality to read `Target` blocks from a binary stream
     * and write them back. It implements the `meta::Stream` interface to handle stream operations
     * specific to target assets.
     */
    class APPLIB_API TargetStream final : public meta::Stream
    {
    public:
        /**
         * @brief Writes a `Target` block to a binary stream.
         *
         * This method serializes the `Target` block, writing its address, header and checksum
         * to the provided binary stream.
         *
         * @param stream The binary stream to write to.
         * @param block The `Target` block to write.
         */
        virtual void writeToStream(astl::bin_stream &stream, meta::Block *block) override;

        /**
         * @brief Reads a `Target` block from a binary stream.
         *
         * This method reads data from the provided stream and constructs a `Target` object.
         *
         * @param stream The binary stream to read from.
         * @return A pointer to the created `Target` block.
         */
        virtual meta::Block *readFromStream(astl::bin_stream &stream) override;
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
     * @brief Stream handler for reading and writing `Library` blocks.
     *
     * This class provides the functionality to read `Library` blocks from a binary stream
     * and write them back. It implements the `meta::Stream` interface to handle stream operations
     * specific to library assets.
     */
    class APPLIB_API LibraryStream final : public meta::Stream
    {
    public:
        /**
         * @brief Writes a `Library` block to a binary stream.
         *
         * This method serializes the `Library` block, writing its file tree to the provided binary stream.
         *
         * @param stream The binary stream to write to.
         * @param block The `Library` block to write.
         */
        virtual void writeToStream(astl::bin_stream &stream, meta::Block *block) override;

        /**
         * @brief Reads a `Library` block from a binary stream.
         *
         * This method reads data from the provided stream and constructs a `Library` object.
         *
         * @param stream The binary stream to read from.
         * @return A pointer to the created `Library` block.
         */
        virtual meta::Block *readFromStream(astl::bin_stream &stream) override;
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
        using iterator = astl::hashmap<std::string, std::shared_ptr<Library>>::iterator;
        using const_iterator = astl::hashmap<std::string, std::shared_ptr<Library>>::const_iterator;

        /**
         * @brief Get the asset libraries size
         */
        size_t size() const { return _libraries.size(); }

        std::shared_ptr<Library> operator[](const std::string &name)
        {
            auto it = _libraries.find(name);
            if (it == _libraries.end()) return nullptr;
            return it->second;
        }

        std::shared_ptr<Library> operator[](const std::string &name) const
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
        astl::hashmap<std::string, std::shared_ptr<Library>> _libraries;
    };
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
    bin_stream &bin_stream::write(const astl::vector<std::shared_ptr<meta::Block>> &meta);

    template <>
    bin_stream &bin_stream::read(astl::vector<std::shared_ptr<meta::Block>> &meta);

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