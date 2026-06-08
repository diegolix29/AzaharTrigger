import sys

def main():
    if len(sys.argv) < 3:
        print("Usage: generate_cheats_header.py <input.json> <output.h>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    try:
        with open(input_file, 'rb') as f:
            data = f.read()
    except Exception as e:
        print(f"Failed to read {input_file}: {e}")
        sys.exit(1)

    # Convert the raw file data into a C++ hex array
    hex_array = ', '.join([f'0x{b:02X}' for b in data])

    header_content = f"""// clang-format off
// Auto-generated file by CMake - DO NOT EDIT
#pragma once
#include <cstdint>

static const uint8_t cheats_json_data[] = {{
    {hex_array}
}};
static const size_t cheats_json_data_size = sizeof(cheats_json_data);
// clang-format on
"""

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(header_content)
    print(f"Successfully generated {output_file}")

if __name__ == "__main__":
    main()