#pragma once

#include <core/io/file.hpp>
#include "asset.hpp"

namespace assets
{
    /**
     * @brief Structure representing information about a target asset.
     */
    struct TargetInfo : InfoHeader
    {
        // Enumeration representing the protocol for the target asset.
        enum Proto : u8
        {
            Unknown, // Unknown or unspecified protocol.
            File,    // Local repository
            Network  // Cloud repository
        } proto;
        std::string url; // URL or path to the target resource.
    };

    /**
     * Class representing a Target asset.
     * The Target class represents a type of asset that doesn't store the resource itself but only
     * information about where the resource is located and its location in the cache.
     */
    class APPLIB_API Target : public Asset
    {
    public:
        /**
         * @brief Constructor for the Target class.
         * @param assetInfo Asset meta header
         * @param targetInfo Specific target asset information.
         * @param targetChecksum Target asset checksum.
         * @param checksum Asset checksum.
         */
        Target(const InfoHeader &assetInfo, const TargetInfo &targetInfo, u32 targetChecksum, u32 checksum = 0)
            : Asset(assetInfo, checksum), _targetInfo(targetInfo)
        {
        }

        /**
         * @brief Retrieves the target asset information.
         * @return Reference to the TargetInfo structure.
         */
        TargetInfo targetInfo() const { return _targetInfo; }

        /**
         * @brief Retrieves the target asset checksum.
         * @return Target asset checksum.
         */
        u32 targetChecksum() const { return _targetChecksum; }

        /**
         * @brief Saves the target asset to a specified filesystem path.
         * @param path Filesystem path to save the target asset.
         * @param compression The compression level to use when saving the target asset.
         * @return True if the target asset is successfully saved, false otherwise.
         */
        bool save(const std::filesystem::path &path, int compression) override;

        /**
         * @brief Reads a Target instance from a file.
         * @param path Filesystem path to read the target asset from.
         * @return Shared pointer to the loaded Target. Returns nullptr in case of failure.
         */
        static std::shared_ptr<Target> readFromFile(const std::filesystem::path &path);

        /**
         * @brief Writes the target asset to a binary stream.
         * @param stream Binary stream to write to.
         * @return True if successful, false otherwise.
         */
        bool writeToStream(BinStream &stream) override;

        /**
         * @brief Reads a Target instance from a binary stream.
         * @param assetInfo General asset information.
         * @param stream Binary stream to read from.
         * @return Shared pointer to the loaded Target. Returns nullptr in case of failure.
         */
        static std::shared_ptr<Target> readFromStream(InfoHeader &assetInfo, BinStream &stream);

        /**
         * @brief Fetches the target asset to cache based on the specified protocol and URL.
         * @param relativeRoot Path to the root of relative paths.
         * @param cachePath Path to the cache file.
         * @return True if successful, false otherwise.
         */
        io::file::ReadState fetchToCache(const std::filesystem::path &relativeRoot,
                                         const std::filesystem::path &cachePath, std::filesystem::copy_options options);

    private:
        TargetInfo _targetInfo;
        u32 _targetChecksum;
    };

    /**
     * @brief Represents a node in the file structure of the asset library.
     */
    struct FileNode
    {
        std::string name;             // Name of the file node.
        DArray<FileNode> children;    // Child nodes of this file node.
        bool isFolder{false};         // Flag indicating if the node is a folder.
        std::shared_ptr<Asset> asset; // Shared pointer to the asset associated with the node.
    };

    /**
     * @brief Class representing an asset library.
     *
     * The Library class serves as a storage for other assets. These assets can either be embedded or act as
     * targets.
     */
    class APPLIB_API Library : public Asset
    {
    public:
        /**
         * @brief Constructor for the Library class.
         * @param assetInfo Asset meta header
         * @param fileTree File structure of the library.
         */
        Library(const InfoHeader &assetInfo, const FileNode &fileTree, u32 checksum = 0)
            : Asset(assetInfo, checksum), _fileTree(fileTree)
        {
        }

        /**
         * @brief Retrieves a list of assets associated with a specified path.
         * @param path Filesystem path to query for assets.
         * @return File search result
         */
        FileNode *getFileNode(const std::filesystem::path &path);

        template <typename T>
        std::shared_ptr<T> getAsset(const std::filesystem::path &path)
        {
            static_assert(std::is_base_of_v<Asset, T>);
            auto fileNode = getFileNode(path);
            return std::dynamic_pointer_cast<T>(fileNode->asset);
        }

        /**
         * @brief Retrieves the file structure of the library.
         * @return Reference to the root of the file tree.
         */
        FileNode &fileTree() { return _fileTree; }

        /**
         * @brief Saves the library to a specified filesystem path.
         * @param path Filesystem path to save the library.
         * @param compression The compression level to use when saving the library.
         * @return True if the library is successfully saved, false otherwise.
         */
        bool save(const std::filesystem::path &path, int compression) override;

        /**
         * @brief Writes the library to a binary stream.
         * @param stream Binary stream to write to.
         * @return True if successful, false otherwise.
         */
        bool writeToStream(BinStream &stream) override
        {
            if (!writeToStream(_fileTree, stream)) return false;
            stream.write(meta);
            return true;
        }

        /**
         * @brief Reads a Library instance from a binary stream.
         * @param assetInfo General asset information.
         * @param stream Binary stream to read from.
         * @return Shared pointer to the loaded Library. Returns nullptr in case of failure.
         */
        static std::shared_ptr<Library> readFromStream(InfoHeader &assetInfo, BinStream &stream);

        /**
         * @brief Reads a Library instance from a file.
         * @param path Filesystem path to read the library from.
         * @return Shared pointer to the loaded Library. Returns nullptr in case of failure.
         */
        static std::shared_ptr<Library> readFromFile(const std::filesystem::path &path);

    private:
        FileNode _fileTree;

        bool writeToStream(const FileNode &node, BinStream &stream);
        static bool readFromStream(FileNode &node, BinStream &stream);
    };

    /**
     * Class responsible for managing asset libraries.
     *
     * The Manager class oversees and provides access to various asset libraries, allowing for efficient
     * organization and retrieval of libraries by their names. It provides iterator functionality for easy
     * traversal of the libraries and ensures streamlined integration with the underlying system through
     * its initialization method.
     */
    class Manager
    {
    public:
        using iterator = HashMap<std::string, std::shared_ptr<Library>>::iterator;
        using const_iterator = HashMap<std::string, std::shared_ptr<Library>>::const_iterator;

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
        HashMap<std::string, std::shared_ptr<Library>> _libraries;
    };
} // namespace assets