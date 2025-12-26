import sys
import os

def convert_lua_to_c_string(lua_file, header_file):
    if not os.path.exists(lua_file):
        print(f"Error: {lua_file} not found.")
        sys.exit(1)

    # Create a variable name based on the filename (e.g., myscript_lua_source)
    base_name = os.path.basename(lua_file).replace('.', '_').replace('-', '_')
    var_name = f"{base_name}_source"

    with open(lua_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    with open(header_file, 'w', encoding='utf-8') as h:
        h.write(f"/* Auto-generated from {lua_file} */\n")
        h.write(f"#ifndef {base_name.upper()}_H\n")
        h.write(f"#define {base_name.upper()}_H\n\n")
        
        h.write(f"const char* {var_name} =\n")
        
        for i, line in enumerate(lines):
            # 1. Escape backslashes first
            # 2. Escape double quotes
            # 3. Strip trailing newline and add \n for C
            escaped_line = line.replace('\\', '\\\\').replace('"', '\\"').rstrip('\n\r')
            
            # Formatting: add the line in quotes with a literal \n at the end
            h.write(f'    "{escaped_line}\\n"')
            
            # Add a semicolon on the last line, otherwise just a newline
            if i == len(lines) - 1:
                h.write(";\n")
            else:
                h.write("\n")
        
        h.write(f"\n#endif /* {base_name.upper()}_H */\n")

    print(f"Successfully created {header_file} with variable: {var_name}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python lua_to_header.py <input.lua> <output.h>")
    else:
        convert_lua_to_c_string(sys.argv[1], sys.argv[2])