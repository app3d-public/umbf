#!/usr/bin/env python3
"""
Get the min image atlas size by the provided asset config.
"""
import argparse
import json
import re


def can_pack_images(images, max_width, max_height, padding=0):
    """
    Check if a list of images can be packed into a given maximum width and height with optional padding.
    """
    free_rectangles = [(0, 0, max_width, max_height)]

    for img_width, img_height in images:
        # Учитываем padding
        img_width += 2 * padding
        img_height += 2 * padding

        placed = False
        best_rect = None
        best_index = -1
        best_area = None

        for i, (x, y, width, height) in enumerate(free_rectangles):
            if img_width <= width and img_height <= height:
                area = width * height
                if best_area is None or area < best_area:
                    best_rect = (x, y, width, height)
                    best_index = i
                    best_area = area

        if best_rect:
            placed = True
            x, y, width, height = best_rect

            new_rect1 = (x + img_width, y, width - img_width, img_height)
            new_rect2 = (x, y + img_height, width, height - img_height)

            free_rectangles[best_index] = free_rectangles[-1]
            free_rectangles.pop()

            if new_rect1[2] > 0 and new_rect1[3] > 0:
                free_rectangles.append(new_rect1)
            if new_rect2[2] > 0 and new_rect2[3] > 0:
                free_rectangles.append(new_rect2)

        if not placed:
            return False
    return True

def find_min_atlas_size(images, initial_max_dim, padding=0):
    """
    Find the minimum size of an atlas that can fit all the given images with optional padding.
    """
    min_size = 1
    max_size = initial_max_dim * 2

    while min_size < max_size:
        mid_size = (min_size + max_size) // 2
        if can_pack_images(images, mid_size, mid_size, padding):
            max_size = mid_size
        else:
            min_size = mid_size + 1

    return min_size

def main():
    """
    Calculate the minimum atlas size based on the provided JSON configuration file and optional padding.

    This function takes a JSON configuration file as input and calculates the minimum atlas size.

    Raises:
        FileNotFoundError: If the specified configuration file does not exist.
        JSONDecodeError: If the configuration file is not a valid JSON file.

    Example:
        $ python atlas_min_size.py -i config.json --padding 5
        Min size: 1024x1024
    """
    parser = argparse.ArgumentParser(description="Calculate the minimum atlas size with optional padding.")
    parser.add_argument("-i", "--input", required=True, help="Path to the JSON configuration file")
    parser.add_argument("--padding", type=int, default=0, help="Optional padding value to apply (default: 0)")
    args = parser.parse_args()

    with open(args.input, "r", encoding="utf-8") as file:
        data = json.load(file)

    image_dimensions = []
    for img_data in data["images"]:
        img_path = img_data["path"]
        match = re.search(r'_(\d+)x(\d+)\.', img_path)
        if match:
            width = int(match.group(1))
            height = int(match.group(2))
            image_dimensions.append((width, height))

    image_dimensions.sort(key=lambda dims: dims[0] * dims[1], reverse=True)

    max_dim = max(data["width"], data["height"])
    min_atlas_size = find_min_atlas_size(image_dimensions, max_dim, args.padding)

    print(f"Min size: {min_atlas_size}x{min_atlas_size}")

if __name__ == "__main__":
    main()
