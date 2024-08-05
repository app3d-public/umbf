#pragma once

#include "asset.hpp"
#include "core/std/stream.hpp"

namespace assets
{
    /**
     * @brief Represents a node of material properties.
     */
    struct MaterialNode
    {
        glm::vec3 rgb; // RGB color value for the material.
        bool textured; // Flag indicating if the material is textured.
        u16 textureID; // Index of the texture. Defaults to -1 indicating no texture.
    };

    struct MaterialInfo
    {
        MaterialNode albedo;
    };

    class APPLIB_API Material : public Asset
    {
    public:
        DArray<std::shared_ptr<Asset>> textures;
        MaterialInfo info;

        Material(const InfoHeader &assetInfo, const DArray<std::shared_ptr<Asset>> &textures,
                 const MaterialInfo &material, u32 checksum = 0)
            : Asset(assetInfo, checksum), textures(textures), info{material}
        {
        }

        /**
         * @brief Saves the material to a specified filesystem path.
         * @param path Filesystem path to save the material.
         * @param compression Compression level to use when saving the material.
         * @return True if the material is successfully saved, false otherwise.
         */
        bool save(const std::filesystem::path &path, int compression) override;

        /**
         * @brief Reads a Material instance from a binary stream.
         * @param assetInfo Information header for the asset.
         * @param stream Binary stream to read from.
         * @return Shared pointer to the loaded Material.
         */
        static APPLIB_API std::shared_ptr<Material> readFromStream(InfoHeader &assetInfo, BinStream &stream);

        /**
         * @brief Reads a Material instance from a file.
         * @param path Filesystem path to read the material from.
         * @return Shared pointer to the loaded Material.
         */
        static APPLIB_API std::shared_ptr<Material> readFromFile(const std::filesystem::path &path);

        /**
         * @brief Writes the material to a binary stream.
         * @param stream Binary stream to write to.
         * @return True if successful, false otherwise.
         */
        bool writeToStream(BinStream &stream) override;
    };

    void writeTexturesToStream(BinStream &stream, const DArray<std::shared_ptr<Asset>> &textures);

    void readTexturesFromStream(BinStream &stream, DArray<std::shared_ptr<Asset>> &textures);
} // namespace assets

template <>
BinStream& BinStream::write(const assets::MaterialNode& src);

template <>
BinStream& BinStream::read(assets::MaterialNode& dst);