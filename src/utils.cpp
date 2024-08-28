#include <assets/utils.hpp>
#include <core/log.hpp>
#include <numeric>

namespace assets
{
    namespace utils
    {
        template <typename T>
        void fillColorPixelsImpl(const glm::vec4 &color, Image2D &imageInfo)
        {
            T *data = (T *)scalable_malloc(imageInfo.imageSize());
            if (color[0] == color[1] && color[0] == color[2] && color[0] == color[3])
                std::fill(data, data + imageInfo.imageSize(), color[0]);
            else
            {
                assert((imageInfo.channelCount == 3 || imageInfo.channelCount == 4) &&
                       "Fill color only supports RGB or RGBA image");
                for (int i = 0; i < imageInfo.imageSize(); i += imageInfo.channelCount)
                    for (int ch = 0; ch < imageInfo.channelCount; ch++) data[i + ch] = color[ch];
            }

            imageInfo.pixels = data;
        }

        void fillColorPixels(const glm::vec4 &color, Image2D &imageInfo)
        {
            switch (imageInfo.imageFormat)
            {
                case vk::Format::eR8G8B8Unorm:
                case vk::Format::eR8G8B8A8Unorm:
                case vk::Format::eR8G8B8Srgb:
                case vk::Format::eR8G8B8A8Srgb:
                case vk::Format::eR8G8B8Uint:
                case vk::Format::eR8G8B8A8Uint:
                    fillColorPixelsImpl<u8>(color, imageInfo);
                    break;
                case vk::Format::eR8G8B8Sint:
                case vk::Format::eR8G8B8A8Sint:
                case vk::Format::eR8G8B8Snorm:
                case vk::Format::eR8G8B8A8Snorm:
                    fillColorPixelsImpl<i8>(color, imageInfo);
                    break;
                case vk::Format::eR16G16B16Unorm:
                case vk::Format::eR16G16B16A16Unorm:
                case vk::Format::eR16G16B16Uint:
                case vk::Format::eR16G16B16A16Uint:
                    fillColorPixelsImpl<u16>(color, imageInfo);
                    break;
                case vk::Format::eR16G16B16Sint:
                case vk::Format::eR16G16B16A16Sint:
                case vk::Format::eR16G16B16Snorm:
                case vk::Format::eR16G16B16A16Snorm:
                    fillColorPixelsImpl<i16>(color, imageInfo);
                    break;
                case vk::Format::eR32G32B32Uint:
                case vk::Format::eR32G32B32A32Uint:
                    fillColorPixelsImpl<u32>(color, imageInfo);
                    break;
                case vk::Format::eR32G32B32Sint:
                case vk::Format::eR32G32B32A32Sint:
                    fillColorPixelsImpl<i32>(color, imageInfo);
                    break;
                case vk::Format::eR16G16B16Sfloat:
                case vk::Format::eR16G16B16A16Sfloat:
                    fillColorPixelsImpl<f16>(color, imageInfo);
                    break;
                case vk::Format::eR32G32B32Sfloat:
                case vk::Format::eR32G32B32A32Sfloat:
                    fillColorPixelsImpl<f32>(color, imageInfo);
                    break;
                default:
                    logWarn("Cannot fill pixel buffer. Unsupported format: %s",
                            vk::to_string(imageInfo.imageFormat).c_str());
                    break;
            }
        }

        template <typename T>
        void copyPixelsToAreaImpl(Image2D &src, const Image2D &dst, const Atlas::Rect &rect)
        {
            const T *pSrc = reinterpret_cast<const T *>(src.pixels);
            T *pDst = reinterpret_cast<T *>(dst.pixels);
            if (rect.x + rect.w <= dst.width && rect.y + rect.h <= dst.height)
                for (size_t y = 0; y < rect.h; ++y)
                    memcpy(pDst + ((rect.y + y) * dst.width + rect.x) * dst.channelCount,
                           pSrc + (y * rect.w) * dst.channelCount, rect.w * dst.channelCount);
            else
                throw std::runtime_error("Dst area is out of image bounds");
        }

        void copyPixelsToArea(Image2D &src, const Image2D &dst, const Atlas::Rect &rect)
        {
            if (src.imageFormat != dst.imageFormat) throw std::runtime_error("Image format mismatch");
            switch (dst.imageFormat)
            {
                case vk::Format::eR8G8B8Unorm:
                case vk::Format::eR8G8B8A8Unorm:
                case vk::Format::eR8G8B8Srgb:
                case vk::Format::eR8G8B8A8Srgb:
                case vk::Format::eR8G8B8Uint:
                case vk::Format::eR8G8B8A8Uint:
                    copyPixelsToAreaImpl<u8>(src, dst, rect);
                    break;
                case vk::Format::eR8G8B8Sint:
                case vk::Format::eR8G8B8A8Sint:
                case vk::Format::eR8G8B8Snorm:
                case vk::Format::eR8G8B8A8Snorm:
                    copyPixelsToAreaImpl<i8>(src, dst, rect);
                    break;
                case vk::Format::eR16G16B16Unorm:
                case vk::Format::eR16G16B16A16Unorm:
                case vk::Format::eR16G16B16Uint:
                case vk::Format::eR16G16B16A16Uint:
                    copyPixelsToAreaImpl<u16>(src, dst, rect);
                    break;
                case vk::Format::eR16G16B16Sint:
                case vk::Format::eR16G16B16A16Sint:
                case vk::Format::eR16G16B16Snorm:
                case vk::Format::eR16G16B16A16Snorm:
                    copyPixelsToAreaImpl<i16>(src, dst, rect);
                    break;
                case vk::Format::eR32G32B32Uint:
                case vk::Format::eR32G32B32A32Uint:
                    copyPixelsToAreaImpl<u32>(src, dst, rect);
                    break;
                case vk::Format::eR32G32B32Sint:
                case vk::Format::eR32G32B32A32Sint:
                    copyPixelsToAreaImpl<i32>(src, dst, rect);
                    break;
                case vk::Format::eR16G16B16Sfloat:
                case vk::Format::eR16G16B16A16Sfloat:
                    copyPixelsToAreaImpl<f16>(src, dst, rect);
                    break;
                case vk::Format::eR32G32B32Sfloat:
                case vk::Format::eR32G32B32A32Sfloat:
                    copyPixelsToAreaImpl<f32>(src, dst, rect);
                    break;
                default:
                    logWarn("Cannot copy pixel buffer. Unsupported format: %s", vk::to_string(dst.imageFormat).c_str());
                    break;
            }
        }

        void *convertImage(const assets::Image2D &image, vk::Format dstFormat, int dstChannels)
        {
            switch (image.imageFormat)
            {
                case vk::Format::eR8G8B8A8Srgb:
                case vk::Format::eR8G8B8A8Uint:
                case vk::Format::eR8G8B8A8Unorm:
                    return getImageConvertBySrc<u8>(dstFormat, image.pixels, image.imageSize() / image.bytesPerChannel,
                                                    image.channelCount, dstChannels);
                case vk::Format::eR8G8B8A8Sint:
                case vk::Format::eR8G8B8A8Snorm:
                    return getImageConvertBySrc<i8>(dstFormat, image.pixels, image.imageSize() / image.bytesPerChannel,
                                                    image.channelCount, dstChannels);
                case vk::Format::eR16G16B16A16Uint:
                    return getImageConvertBySrc<u16>(dstFormat, image.pixels, image.imageSize() / image.bytesPerChannel,
                                                     image.channelCount, dstChannels);
                case vk::Format::eR32G32B32A32Uint:
                    return getImageConvertBySrc<u32>(dstFormat, image.pixels, image.imageSize() / image.bytesPerChannel,
                                                     image.channelCount, dstChannels);
                case vk::Format::eR16G16B16A16Sfloat:
                    return getImageConvertBySrc<f16>(dstFormat, image.pixels, image.imageSize() / image.bytesPerChannel,
                                                     image.channelCount, dstChannels);

                case vk::Format::eR32G32B32A32Sfloat:
                    return getImageConvertBySrc<f32>(dstFormat, image.pixels, image.imageSize() / image.bytesPerChannel,
                                                     image.channelCount, dstChannels);
                default:
                    return nullptr;
            }
        }

        void filterMatAssignments(const DArray<std::shared_ptr<MaterialInfo>> &matMeta,
                                  const DArray<std::shared_ptr<MatRangeAssignAtrr>> &assignes, size_t faceCount,
                                  u32 defaultMatID, DArray<std::shared_ptr<MatRangeAssignAtrr>> &dst)
        {
            auto defaultAssign = std::make_shared<assets::MatRangeAssignAtrr>();
            defaultAssign->matID = defaultMatID;
            defaultAssign->faces.resize(faceCount);
            std::iota(defaultAssign->faces.begin(), defaultAssign->faces.end(), 0);

            if (assignes.empty())
                dst.push_back(defaultAssign);
            else
            {
                DArray<bool> faceIncluded(faceCount, false);

                for (const auto &assign : assignes)
                    for (const auto &face : assign->faces) faceIncluded[face] = true;

                defaultAssign->faces.erase(std::remove_if(defaultAssign->faces.begin(), defaultAssign->faces.end(),
                                                          [&](u32 index) { return faceIncluded[index]; }),
                                           defaultAssign->faces.end());

                if (!defaultAssign->faces.empty()) dst.push_back(defaultAssign);
                for (const auto &assign : assignes) dst.push_back(assign);
            }
        }
    } // namespace utils
} // namespace assets