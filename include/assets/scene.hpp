#pragma once

#include <core/std/enum.hpp>
#include "asset.hpp"

namespace assets
{
    struct Object
    {
        struct Transform
        {
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 rotation = glm::vec3(0.0f);
            glm::vec3 scale = glm::vec3(1.0f);
        } transform;

        i32 matID = -1;
        meta::Block *meta = nullptr;

        ~Object() { delete meta; }
    };

    /**
     * @brief Class representing a scene asset.
     * @details Extends the Asset class.
     */
    class APPLIB_API Scene final : public Asset
    {
    public:
        struct MaterialNode
        {
            std::string name;
            std::shared_ptr<Asset> asset;
        };

        DArray<std::shared_ptr<Object>> objects;
        DArray<std::shared_ptr<Asset>> textures;
        DArray<MaterialNode> materials;

        /**
         * @brief Constructor for the AssetScene class.
         * @param assetInfo Information about the asset.
         * @param objects Array of objects associated with the scene.
         * @param textures Array of textures associated with the scene.
         * @param materials Array of materials associated with the scene.
         */
        Scene(const InfoHeader &assetInfo, const DArray<std::shared_ptr<Object>> &objects,
              const DArray<std::shared_ptr<Asset>> &textures, const DArray<MaterialNode> &materials, u32 checksum = 0)
            : Asset(assetInfo, checksum), objects(objects), textures(textures), materials(materials)
        {
        }

        /**
         * @brief Saves the scene to a specified filesystem path.
         * @param path Filesystem path to save the scene.
         * @param compression Compression level to use when saving the scene.
         * @return True if the scene is successfully saved, false otherwise.
         */
        bool save(const std::filesystem::path &path, int compression) override;

        /**
         * @brief Writes the scene to a binary stream.
         * @param stream Binary stream to write to.
         * @return True if successful, false otherwise.
         */
        bool writeToStream(BinStream &stream) override;

        /**
         * @brief Reads an AssetScene instance from a binary stream.
         * @param assetInfo Information header for the asset.
         * @param stream Binary stream to read from.
         * @return Shared pointer to the loaded AssetScene.
         */
        static APPLIB_API std::shared_ptr<Scene> readFromStream(InfoHeader &assetInfo, BinStream &stream);

        /**
         * @brief Reads an AssetScene instance from a file.
         * @param path Filesystem path to read the scene from.
         * @return Shared pointer to the loaded AssetScene.
         */
        static APPLIB_API std::shared_ptr<Scene> readFromFile(const std::filesystem::path &path);
    };

    /*********************************
     **
     ** Default metadata
     **
     *********************************/

    namespace meta
    {
        constexpr u32 sign_block_scene = SIGN_APP_PART_DEFAULT << 16 | 0xA9FD;
        constexpr u32 sign_block_mesh = SIGN_APP_PART_DEFAULT << 16 | 0x57CC;

        struct SceneInfo : public Block
        {
            std::string author;
            std::string info;
            u32 version;

            virtual const u32 signature() const { return sign_block_scene; }
        };

        class APPLIB_API SceneInfoStream final : public Stream
        {
        public:
            virtual meta::Block *readFromStream(BinStream &stream) override;

            virtual void writeToStream(BinStream &stream, meta::Block *block) override;
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
                DArray<u32> vertices; ///< List of indices pointing to vertices in this group.
                DArray<u32> faces;    ///< List of indices of faces that reference these vertices.
            };

            // Represents a polygon face.
            struct Face
            {
                DArray<VertexRef> vertices; ///< List of vertex references that define the face.
                glm::vec3 normal;           ///< The normal vector of the face
                u32 startID;                ///< Starting index in the index buffer for this face.
                u16 indexCount;             ///< Number of indices that define this face.
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
                DArray<Vertex> vertices;          ///< Array containing all vertices of the model.
                DArray<VertexGroup> vertexGroups; ///< Array containing groups of vertices.
                DArray<Face> faces;               ///< Array of faces that make up the model.
                DArray<u32> indices;              ///< Array of indices for rendering the model.
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
            struct MeshBlock : public Block
            {
                Model model;                       ///< The 3D model data contained in this mesh block.
                DArray<bary::Vertex> baryVertices; ///< Array of vertices with barycentric coordinates.

                /**
                 * @brief Returns the signature of the block.
                 * @return The signature of the block.
                 */
                virtual const u32 signature() const { return sign_block_mesh; }
            };
            // Stream class for reading and writing mesh data.
            class APPLIB_API MeshStream : public Stream
            {
            public:
                /**
                 * @brief Reads a block from the binary stream.
                 * @param stream The binary stream to read from.
                 * @return A pointer to the read block.
                 */
                virtual meta::Block *readFromStream(BinStream &stream) override;

                /**
                 * @brief Writes a block to the binary stream.
                 * @param stream The binary stream to write to.
                 * @param block The block to write.
                 */
                virtual void writeToStream(BinStream &stream, meta::Block *block) override;
            };
        } // namespace mesh
    } // namespace meta
} // namespace assets