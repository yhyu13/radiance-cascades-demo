import re
import os

def remove_binding_qualifiers(filepath):
    """Remove layout(binding = X) from shader file"""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Pattern 1: Remove binding from layout qualifiers like "layout(rgba16f, binding = 0)"
    # Replace with just "layout(rgba16f)"
    pattern1 = r'layout\(([^)]*),\s*binding\s*=\s*\d+\)'
    replacement1 = r'layout(\1)'
    content = re.sub(pattern1, replacement1, content)
    
    # Pattern 2: Handle cases where binding is the only parameter (less common)
    # "layout(binding = 0)" -> should not happen in our shaders but handle it
    pattern2 = r'layout\s*\(\s*binding\s*=\s*\d+\s*\)'
    replacement2 = r'layout()'
    content = re.sub(pattern2, replacement2, content)
    
    # Pattern 3: Fix debug code that incorrectly uses 'binding' as variable
    # Comment out lines like "if (binding == X) {" 
    pattern3 = r'^(\s*)if\s*\(\s*binding\s*==\s*\d+\s*\)\s*\{'
    def replace_debug(match):
        indent = match.group(1)
        return f'{indent}// DEBUG DISABLED: {match.group(0).strip()}'
    content = re.sub(pattern3, replace_debug, content, flags=re.MULTILINE)
    
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)
    
    print(f"[OK] Fixed {os.path.basename(filepath)}")

# Fix all compute shaders
shader_dir = r"c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\res\shaders"
shaders_to_fix = [
    "radiance_3d.comp",
    "sdf_3d.comp", 
    "voxelize.comp",
    "inject_radiance.comp"  # Already fixed version, but check for remaining bindings
]

for shader in shaders_to_fix:
    filepath = os.path.join(shader_dir, shader)
    if os.path.exists(filepath):
        remove_binding_qualifiers(filepath)
    else:
        print(f"[WARN] Not found: {shader}")

print("\n[OK] All shaders processed!")
