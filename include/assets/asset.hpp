#pragma once

#include <core/std/basic_types.hpp>
#include <core/std/forward_list.hpp>
#include <core/std/stream.hpp>
#include <filesystem>
#include <memory>

#ifndef APP_SIGN_DEFAULTS
    #define APP_SIGN_DEFAULTS
    #define SIGN_APP_PART_DEFAULT 0x5828
#endif

namespace assets
{
    // The file signature of the asset file format
    constexpr u8 SIGNATURE[4] = {0xFF, 0xBE, 0xCF, 0xB8};

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

    APPLIB_API std::string toString(Type type);

    /**
     * @struct InfoHeader
     * @brief Contains metadata about an asset.
     *
     * This struct holds various metadata details about an asset, including its name, type, and compression
     * status.
     */
    struct InfoHeader
    {
        Type type = Type::Invalid; // Asset type
        bool compressed;           // Indicates whether the asset data block is compressed
    };

    namespace meta
    {
        struct Header
        {
            u32 signature;
            u64 blockSize;
        };
        struct Block
        {
            virtual ~Block() = default;

            virtual const u32 signature() const { return 0x0; }
        };

        class APPLIB_API Stream
        {
        public:
            virtual ~Stream() = default;

            virtual Block *readFromStream(BinStream &stream) = 0;

            virtual void writeToStream(BinStream &stream, Block *block) = 0;
        };

        constexpr u32 sign_block_external = SIGN_APP_PART_DEFAULT << 16 | 0x3F84;

        APPLIB_API void addStream(u32 signature, Stream *stream);

        APPLIB_API void clearStreams();

        APPLIB_API Stream *getStream(u32 signature);
    } // namespace meta

    /**
     * @class Asset
     * @brief Represents a generic asset.
     *
     * The Asset class is an abstract base class designed to represent and manage various types of assets.
     * It provides functionalities for saving and loading assets from files and streams.
     */
    class APPLIB_API Asset
    {
    public:
        ForwardList<std::shared_ptr<meta::Block>> meta;

        /**
         * @brief Constructor for the Asset class.
         *
         * Initializes a new instance of the Asset class with the specified information and checksum.
         *
         * @param info Asset meta data.
         * @param checksum The checksum value of the asset.
         *
         * @note If the checksum is euqal to 0, then checksum validation will not be performed
         */
        Asset(const InfoHeader &info = {}, u32 checksum = 0) : _info(info), _checksum(checksum) {}

        /// Retrieves the asset's information.
        const InfoHeader &info() const { return _info; }

        /// Retrieves the asset's checksum.
        u32 checksum() const { return _checksum; }

        /**
         * @brief Saves the asset to a file.
         *
         * This method saves the asset to the specified file path.
         *
         * @param path The path to the destination file.
         * @param compression The compression level to use when saving the asset.
         * @return Returns true if the save operation was successful, false otherwise.
         */
        virtual bool save(const std::filesystem::path &path, int compression = 5) = 0;

        /**
         * @brief Writes the asset to a binary stream.
         *
         * @param stream The binary stream to which the asset will be written.
         * @return Returns true if the write operation was successful, false otherwise.
         */
        virtual bool writeToStream(BinStream &stream) = 0;

        /**
         * @brief Reads and creates an asset from a binary stream.
         *
         * This static method reads asset data from the provided binary stream and creates an asset instance.
         *
         * @param assetInfo The asset information that will be updated during the read operation.
         * @param stream The binary stream from which the asset will be read.
         * @return A shared pointer to the created asset.
         */
        static APPLIB_API std::shared_ptr<Asset> readFromStream(InfoHeader &assetInfo, BinStream &stream);

        virtual ~Asset() = default;

        /**
         * @brief Reads and creates an asset from a binary stream.
         * @param path The path to the asset.
         * @return A shared pointer to the created asset.
         **/
        static APPLIB_API std::shared_ptr<Asset> readFromFile(const std::filesystem::path &path);

    protected:
        InfoHeader _info;
        u32 _checksum{0};

        bool saveFile(const std::filesystem::path &path, BinStream &src, int compression);
        static bool loadFile(const std::filesystem::path &path, BinStream &dst, InfoHeader &info);
    };

    class InvalidAsset final : public Asset
    {
    public:
        InvalidAsset() : Asset({Type::Invalid, false}) {}

        virtual bool writeToStream(BinStream &) override { return false; }

        virtual bool save(const std::filesystem::path &, int) override { return false; }
    };

    /*********************************
     **
     ** Default metadata
     **
     *********************************/
    namespace meta
    {
        struct ExternalBlock : public meta::Block
        {
            char *data = nullptr;
            u64 dataSize = 0;

            virtual const u32 signature() const { return sign_block_external; }

            ~ExternalBlock() { delete data; }
        };

        class APPLIB_API ExternalStream final : public meta::Stream
        {
        public:
            virtual meta::Block *readFromStream(BinStream &stream) override;

            virtual void writeToStream(BinStream &stream, meta::Block *block) override;
        };
    } // namespace meta
} // namespace assets

template <>
APPLIB_API BinStream &BinStream::write(const assets::InfoHeader &src);

template <>
APPLIB_API BinStream &BinStream::read(assets::InfoHeader &dst);

template<>
APPLIB_API BinStream& BinStream::write(const ForwardList<std::shared_ptr<assets::meta::Block>>& meta);

template<>
APPLIB_API BinStream& BinStream::read(ForwardList<std::shared_ptr<assets::meta::Block>>& meta);