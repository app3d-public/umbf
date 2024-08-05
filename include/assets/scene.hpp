#pragma once

#include <core/std/enum.hpp>
#include "asset.hpp"

#ifndef APP_SIGN_DEFAULTS
    #define APP_SIGN_DEFAULTS
    #define SIGN_APP_PART_DEFAULT 0x5828
#endif

constexpr u32 sign_meta_block_mesh = SIGN_APP_PART_DEFAULT << 16 | 0x57CC;

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

        struct MetaBlock
        {
            virtual ~MetaBlock() = default;

            virtual const u32 signature() const { return 0x0; }
        } *meta = nullptr;

        ~Object() { delete meta; }

        struct MetaHeader;
        class MetaStream;
    };

    struct Object::MetaHeader
    {
        u8 flags;
        u32 signature;
        u64 blockSize;
    };

    class Object::MetaStream
    {
    public:
        virtual ~MetaStream() = default;

        virtual Object::MetaBlock *readFromStream(BinStream &stream) = 0;

        virtual void writeToStream(BinStream &stream, Object::MetaBlock *block) = 0;
    };

    APPLIB_API void addObjectMetaStream(u32 signature, Object::MetaStream *stream);

    APPLIB_API void clearMetaStreams();

    /**
     * @brief Class representing a scene asset.
     * @details Extends the Asset class.
     */
    class APPLIB_API Scene final : public Asset
    {
    public:
        struct MetaInfo
        {
            std::string author = "";
            std::string info = "";
            u32 appVersion = 0x0;
        } meta;

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
         * @param meta Information about the scene.
         * @param objects Array of objects associated with the scene.
         * @param textures Array of textures associated with the scene.
         * @param materials Array of materials associated with the scene.
         */
        Scene(const InfoHeader &assetInfo, MetaInfo meta, const DArray<std::shared_ptr<Object>> &objects,
              const DArray<std::shared_ptr<Asset>> &textures, const DArray<MaterialNode> &materials, u32 checksum = 0)
            : Asset(assetInfo, checksum), meta(meta), objects(objects), textures(textures), materials(materials)
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

    namespace mesh
    {
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

        struct VertexRef
        {
            u32 vertexGroup;
            u32 vertex;
        };

        struct VertexGroup
        {
            DArray<u32> vertices;
            DArray<u32> faces;
        };

        struct Face
        {
            DArray<VertexRef> vertices;
            glm::vec3 normal;
            u32 startID;
            u16 indexCount;
        };

        struct AABB
        {
            alignas(16) glm::vec3 min = glm::vec3(std::numeric_limits<f32>::max());
            alignas(16) glm::vec3 max = glm::vec3(std::numeric_limits<f32>::lowest());
        };

        struct Model
        {
            DArray<Vertex> vertices;
            DArray<VertexGroup> vertexGroups;
            DArray<Face> faces;
            DArray<u32> indices;
            AABB aabb;
        };

        namespace bary
        {
            struct Vertex
            {
                glm::vec3 pos{0.0f, 0.0f, 0.0f};
                glm::vec3 barycentric{0.0f, 0.0f, 0.0f};

                bool operator==(const Vertex &other) const
                {
                    return pos == other.pos && barycentric == other.barycentric;
                }
            };
        }; // namespace bary

        struct MeshBlock : public Object::MetaBlock
        {
            Model model;
            DArray<bary::Vertex> barycentricVertices;

            virtual const u32 signature() const { return sign_meta_block_mesh; }
        };
    } // namespace mesh
} // namespace assets