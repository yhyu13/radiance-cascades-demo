import re

filepath = r"c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\res\shaders\sdf_3d.comp"

with open(filepath, 'r', encoding='utf-8') as f:
    content = f.read()

# Fix 1: Change version from 450 to 430
content = content.replace('#version 450 core', '#version 430 core')

# Fix 2: Remove binding qualifiers
content = re.sub(r'layout\(([^)]*),\s*binding\s*=\s*\d+\)', r'layout(\1)', content)

# Fix 3: Change rgb32f to rgba32f
content = content.replace('layout(rgb32f)', 'layout(rgba32f)')

# Fix 4: Inline safeLoad in getDensity function
old_getDensity = '''float getDensity(ivec3 pos) {
    return safeLoad(uVoxelGrid, pos).a;
}'''

new_getDensity = '''float getDensity(ivec3 pos) {
    if (any(lessThan(pos, ivec3(0))) || any(greaterThanEqual(pos, uVolumeSize))) {
        return 0.0;
    }
    return imageLoad(uVoxelGrid, pos).a;
}'''

content = content.replace(old_getDensity, new_getDensity)

# Fix 5: Inline safeLoad call in main loop (around line 119)
old_safeLoad_call = '''        // Load neighbor's distance value
        vec4 neighborData = safeLoad(oSDF, samplePos);'''

new_safeLoad_call = '''        // Load neighbor's distance value with bounds checking
        vec4 neighborData;
        if (any(lessThan(samplePos, ivec3(0))) || any(greaterThanEqual(samplePos, uVolumeSize))) {
            neighborData = vec4(INF, 0.0, 0.0, 0.0);
        } else {
            neighborData = imageLoad(oSDF, samplePos);
        }'''

content = content.replace(old_safeLoad_call, new_safeLoad_call)

# Fix 6: Comment out debug code with proper syntax
content = re.sub(
    r'^(\s*)// DEBUG DISABLED: if \(binding == \d+\) \{$',
    r'\1// TODO: Optional Voronoi diagram output\n\1// if (some_condition) {',
    content,
    flags=re.MULTILINE
)

# Fix 7: Close the commented blocks properly - find orphaned closing braces
# This is tricky, so let's just comment them out too
content = re.sub(
    r'^(\s+)\}\s*$',
    lambda m: m.group(0) if not re.search(r'^\s*//.*if.*binding', content[max(0, content.rfind(m.group(0))-200):content.rfind(m.group(0))], re.MULTILINE) else m.group(1) + '// }',
    content,
    flags=re.MULTILINE
)

with open(filepath, 'w', encoding='utf-8') as f:
    f.write(content)

print("[OK] Fixed sdf_3d.comp")
