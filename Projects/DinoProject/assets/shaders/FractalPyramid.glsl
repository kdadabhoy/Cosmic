#type fragment
#version 450 core

// Do NOT declare iTime, iResolution, or uniform floats here.
// The Preprocessor injects these automatically based on your engine's registry.

vec3 palette(float d) {
    return mix(vec3(0.2, 0.7, 0.9), vec3(1.0, 0.0, 1.0), d);
}

vec2 rotate(vec2 p, float a) {
    float c = cos(a);
    float s = sin(a);
    return p * mat2(c, s, -s, c);
}

float map(vec3 p) {
    // iTime is now safely defined by your engine's preprocessor
    for(int i = 0; i < 8; ++i) {
        float t = iTime * 0.2;
        p.xz = rotate(p.xz, t);
        p.xy = rotate(p.xy, t * 1.89);
        p.xz = abs(p.xz) - 0.5;
    }
    return dot(sign(p), p) / 5.0;
}

vec4 rm(vec3 ro, vec3 rd) {
    float t = 0.0;
    vec3 col = vec3(0.0);
    float d;
    
    // Set a explicit background color to prevent the "black box" issue
    vec3 backgroundColor = vec3(0.02, 0.01, 0.05);

    for(int i = 0; i < 64; i++){
        vec3 p = ro + rd * t;
        d = map(p) * 0.5;
        
        if(d < 0.02) break;
        if(t > 100.0) {
            col = backgroundColor;
            break;
        }
        
        col += palette(length(p) * 0.1) / (400.0 * (d + 0.01));
        t += d;
    }
    return vec4(col, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Note: Your preprocessor uses u_ViewportSize for iResolution
    vec2 uv = (fragCoord - (iResolution.xy * 0.5)) / iResolution.x;
    vec3 ro = vec3(0.0, 0.0, -50.0);
    ro.xz = rotate(ro.xz, iTime);
    
    vec3 cf = normalize(-ro);
    vec3 cs = normalize(cross(cf, vec3(0.0, 1.0, 0.0)));
    vec3 cu = normalize(cross(cf, cs));
    
    vec3 rd = normalize(cf * 3.0 + uv.x * cs + uv.y * cu);
    
    fragColor = rm(ro, rd);
}