// NOTE: This shader is designed to be processed by the Cosmic Engine preprocessor.
// It will automatically inject necessary headers, uniform declarations (u_Time, u_ViewportSize),
// and the main() wrapper function if they are omitted.

vec3 palette(float d){
    return mix(vec3(0.2, 0.7, 0.9), vec3(1.0, 0.0, 1.0), d);
}

vec2 rotate(vec2 p, float a){
    float c = cos(a);
    float s = sin(a);
    return p * mat2(c, s, -s, c);
}

float map(vec3 p){
    for(int i = 0; i < 8; ++i){
        float t = iTime * 0.2;
        p.xz = rotate(p.xz, t);
        p.xy = rotate(p.xy, t * 1.89);
        p.xz = abs(p.xz);
        p.xz -= 0.5;
    }
    return dot(sign(p), p) / 5.0;
}

vec4 rm(vec3 ro, vec3 rd){
    float t = 0.0;
    vec3 col = vec3(0.0);
    float alpha = 0.0;
    float d;
    
    for(int i = 0; i < 64; i++){
        vec3 p = ro + rd * t;
        d = map(p) * 0.5;
        
        if(d < 0.01){
            alpha = 1.0;
            break;
        }
        if(t > 100.0){
            break;
        }
        
        vec3 stepCol = palette(length(p) * 0.1) / (400.0 * (d + 0.01));
        col += stepCol;
        alpha += clamp(stepCol.r + stepCol.g + stepCol.b, 0.0, 1.0);
        t += d;
    }
    return vec4(col, alpha);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - (iResolution.xy * 0.5)) / iResolution.x;
    vec3 ro = vec3(0.0, 0.0, -50.0);
    ro.xz = rotate(ro.xz, iTime);
    
    vec3 cf = normalize(-ro);
    vec3 cs = normalize(cross(cf, vec3(0.0, 1.0, 0.0)));
    vec3 cu = normalize(cross(cf, cs));
    
    vec3 uuv = ro + cf * 3.0 + uv.x * cs + uv.y * cu;
    vec3 rd = normalize(uuv - ro);
    
    fragColor = rm(ro, rd);
}

/** SHADERDATA
{
    "title": "fractal pyramid",
    "description": "Fractal raymarched object with alpha accumulation.",
    "model": "car"
}
*/