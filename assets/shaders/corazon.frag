#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

uniform sampler2D texturaTejido;
uniform sampler2D texturaNormales;
uniform sampler2D texturaRoughness;

uniform bool usarTextura;
uniform bool usarNormalMap;
uniform bool usarRoughness;
uniform bool modoMapaEstres;
uniform float transparencia;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 viewPos;
uniform vec3 objectColor;

void main() {
    vec3 norm = normalize(Normal);

    if (usarNormalMap) {
        vec3 normalMap = texture(texturaNormales, TexCoords).rgb * 2.0 - 1.0;
        norm = normalize(norm + normalMap * 0.85);
    }

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 lightDir = normalize(lightPos - FragPos);
    vec3 halfDir = normalize(lightDir + viewDir);

    vec3 albedo = objectColor;
    if (usarTextura) {
        albedo *= texture(texturaTejido, TexCoords).rgb;
    }

    // Modo Mapa de Tensión Miocárdica
    if (modoMapaEstres) {
        float tension = smoothstep(-0.6, 0.8, FragPos.y);
        vec3 colorFrio = vec3(0.05, 0.25, 0.95);  // Baja tensión (Base)
        vec3 colorCaliente = vec3(1.0, 0.05, 0.0); // Alta tensión (Ventrículos)
        albedo = mix(colorCaliente, colorFrio, tension);
    }

    float roughness = 0.22;
    if (usarRoughness) {
        roughness = texture(texturaRoughness, TexCoords).r;
    }

    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor * albedo;
    vec3 ambient = 0.25 * lightColor * albedo;

    // Subsurface Scattering (SSS - Translucidez tisular)
    vec3 sssLightDir = lightDir + norm * 0.4;
    float sssDot = max(dot(viewDir, -sssLightDir), 0.0);
    float sssPow = pow(sssDot, 3.2);
    vec3 sss = sssPow * vec3(0.95, 0.08, 0.04) * 1.1;

    // Specular Anisotrópico (Fibras musculares húmedas)
    vec3 tangent = normalize(cross(norm, vec3(0.0, 1.0, 0.0)));
    float dotTH = dot(tangent, halfDir);
    float sinTH = sqrt(max(0.0, 1.0 - dotTH * dotTH));
    float specAniso = pow(sinTH, mix(160.0, 12.0, roughness));
    vec3 specular = specAniso * lightColor * (1.1 - roughness);

    // Fresnel Rim Lighting (Luz de quirófano en contornos)
    float fresnelFactor = pow(1.0 - max(dot(norm, viewDir), 0.0), 3.2);
    vec3 fresnelReflect = fresnelFactor * vec3(0.85, 0.95, 1.0) * 0.55;

    vec3 finalColor = ambient + diffuse + sss + specular + fresnelReflect;
    FragColor = vec4(finalColor, transparencia);
}