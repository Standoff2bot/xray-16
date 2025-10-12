#version 450

layout(location = 0) in vec3 in_world_normal;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in float in_world_z;
layout(location = 3) in float in_view_z;
layout(location = 4) in float in_clip_z;
layout(location = 5) in float in_clip_w;

layout(location = 0) out vec4 out_color;

// Fragment debug buffer - data[0] controls debug mode
layout(binding = 3, std430) buffer FragDebugBuffer {
    uint recorded;
    float data[4];  // data[0] = debug_mode (0-8)
} frag_debug[];

void main() {
    // Read debug mode from SSBO
    uint debug_mode = uint(frag_debug[0].data[0]);

    // Normalize the interpolated normal
    vec3 normal = normalize(in_world_normal);

    vec3 final_color;

    switch(debug_mode) {
        case 0: // Normal lighting (default)
        default:
            {
                vec3 base_color = vec3(0.75, 0.75, 0.75);
                vec3 light_dir = normalize(vec3(0.5, 0.8, 0.6));
                vec3 view_dir = normalize(vec3(0.0, 0.0, 1.0));

                vec3 ambient = vec3(0.25, 0.25, 0.25) * base_color;
                float NdotL = max(dot(normal, light_dir), 0.0);
                vec3 diffuse = base_color * NdotL * 0.7;

                vec3 half_vec = normalize(light_dir + view_dir);
                float NdotH = max(dot(normal, half_vec), 0.0);
                float specular_power = 32.0;
                float specular_strength = 0.3;
                vec3 specular = vec3(1.0, 1.0, 1.0) * pow(NdotH, specular_power) * specular_strength;

                final_color = ambient + diffuse + specular;
            }
            break;

        case 1: // Face orientation (gl_FrontFacing)
            // Green = front face, Red = back face
            if (gl_FrontFacing) {
                final_color = vec3(0.0, 1.0, 0.0);  // Green for front faces
            } else {
                final_color = vec3(1.0, 0.0, 0.0);  // Red for back faces (shouldn't see with culling)
            }
            break;

        case 2: // Normal direction visualization
            // Map normal to RGB (shows which way triangles face)
            final_color = normal * 0.5 + 0.5;  // Remap from [-1,1] to [0,1]
            break;

        case 3: // Depth visualization
            // Near = blue, Far = red
            float depth = gl_FragCoord.z;  // Raw depth value [0,1]
            final_color = vec3(depth, 0.0, 1.0 - depth);
            break;

        case 4: // UV coordinates
            // U = red, V = green
            final_color = vec3(in_uv, 0.0);
            break;

        case 5: // Normal Z component only
            // Positive Z (facing camera) = white, Negative Z = black
            float nz = normal.z * 0.5 + 0.5;
            final_color = vec3(nz);
            break;

        case 6: // Checkerboard pattern based on gl_FrontFacing
            // Helps visualize if triangles are flipping
            float checker = mod(floor(gl_FragCoord.x / 10.0) + floor(gl_FragCoord.y / 10.0), 2.0);
            if (gl_FrontFacing) {
                final_color = mix(vec3(0.0, 0.5, 0.0), vec3(0.0, 1.0, 0.0), checker);
            } else {
                final_color = mix(vec3(0.5, 0.0, 0.0), vec3(1.0, 0.0, 0.0), checker);
            }
            break;

        case 7: // Two-sided visualization
            // Front = normal shading, Back = bright pink
            if (gl_FrontFacing) {
                vec3 base_color = vec3(0.75, 0.75, 0.75);
                vec3 light_dir = normalize(vec3(0.5, 0.8, 0.6));
                float NdotL = max(dot(normal, light_dir), 0.0);
                final_color = base_color * (0.3 + 0.7 * NdotL);
            } else {
                // Bright pink for back faces (shouldn't see this if culling works)
                final_color = vec3(1.0, 0.0, 1.0);
            }
            break;

        case 8: // World position gradient
            // Shows if vertices are in expected positions
            vec3 pos_color = vec3(
                fract(in_world_z * 0.1),      // Z position as red gradient
                fract(in_view_z * 0.1),       // View Z as green gradient
                fract(in_clip_z)               // Clip Z as blue
            );
            final_color = pos_color;
            break;
    }

    // Clamp and output
    final_color = clamp(final_color, 0.0, 1.0);
    out_color = vec4(final_color, 1.0);

    // Store debug info back to SSBO for the first fragment
    if (gl_FragCoord.x < 1.0 && gl_FragCoord.y < 1.0) {
        frag_debug[0].recorded = 1u;
        frag_debug[0].data[1] = gl_FrontFacing ? 1.0 : 0.0;
        frag_debug[0].data[2] = normal.z;
        frag_debug[0].data[3] = gl_FragCoord.z;
    }
}
