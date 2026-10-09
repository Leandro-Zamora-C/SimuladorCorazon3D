#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform float tiempo;
uniform float bpm;

void main() {
    vec3 pos = aPos;

    // Onda de contracción peristáltica (viaja de la base al ápex)
    float frecuencia = (bpm / 60.0) * 6.28318;
    float desfaseVertical = pos.y * 2.5; 
    float latido = sin(tiempo * frecuencia - desfaseVertical);

    // Mayor contracción en el ventrículo (parte inferior)
    float factorMuscular = (pos.y < 0.3) ? (1.2 - pos.y) : 0.3;
    pos += aNormal * (latido * 0.07 * factorMuscular);

    FragPos = vec3(model * vec4(pos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords;

    gl_Position = projection * view * vec4(FragPos, 1.0);
}