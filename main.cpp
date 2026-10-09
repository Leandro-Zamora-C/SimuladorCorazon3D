#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <algorithm>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

// =====================================================================
//                        SIMULADOR CARDIACO 3D
// =====================================================================

// Variables de camara interactiva con interpolacion suave (Lerp)
float camDistancia = 3.5f;
float targetDistancia = 3.5f;
float camYaw = 0.0f;
float targetYaw = 0.0f;
float camPitch = 0.0f;
float targetPitch = 0.0f;
bool haciendoArrastre = false;
double lastX = 0.0, lastY = 0.0;

const float PI_F = 3.14159265359f;

// ---------------------------------------------------------------------
//  Particulas de sangre (interior del corazon)
// ---------------------------------------------------------------------
struct ParticulaSangre {
    glm::vec3 posicion;
    glm::vec3 velocidad;
    float vida;
    float vidaMax;
};

const int MAX_PARTICULAS = 1000;
std::vector<ParticulaSangre> particulas(MAX_PARTICULAS);

void inicializarParticulas() {
    for (int i = 0; i < MAX_PARTICULAS; ++i) {
        particulas[i].posicion = glm::vec3(
            ((rand() % 100) / 100.0f - 0.5f) * 0.25f,
            0.7f + ((rand() % 100) / 100.0f) * 0.2f,
            ((rand() % 100) / 100.0f - 0.5f) * 0.25f
        );
        particulas[i].velocidad = glm::vec3(
            ((rand() % 100) / 100.0f - 0.5f) * 0.05f,
            -0.85f - ((rand() % 100) / 100.0f) * 0.35f,
            ((rand() % 100) / 100.0f - 0.5f) * 0.05f
        );
        particulas[i].vidaMax = 1.0f + ((rand() % 100) / 100.0f) * 1.5f;
        particulas[i].vida = (float)(rand() % 100) / 100.0f * particulas[i].vidaMax;
    }
}

void actualizarParticulas(float dt, float bpm, int patologia) {
    float factorVelocidad = bpm / 60.0f;
    if (patologia == 3) factorVelocidad *= (0.3f + (rand() % 100) / 100.0f);

    for (auto& p : particulas) {
        p.vida -= dt;
        p.posicion += p.velocidad * dt * factorVelocidad;

        if (p.vida <= 0.0f || p.posicion.y < -1.1f) {
            p.vida = p.vidaMax;
            p.posicion = glm::vec3(
                ((rand() % 100) / 100.0f - 0.5f) * 0.2f,
                0.7f,
                ((rand() % 100) / 100.0f - 0.5f) * 0.2f
            );
        }
    }
}

// ---------------------------------------------------------------------
//  Vasos sanguineos (aorta, vena cava, arteria pulmonar)
//  con particulas de flujo que recorren cada vaso
// ---------------------------------------------------------------------
struct Vaso {
    std::vector<glm::vec3> ruta;   // puntos muestreados de la curva
    glm::vec3 color;
    float radio;
};

// ---------------------------------------------------------------------
//  Particulas de flujo dentro de los vasos sanguineos
// ---------------------------------------------------------------------
struct ParticulaVaso {
    int vaso;
    float t;          // 0..1 a lo largo del vaso
    float vel;
    glm::vec3 jitter; // desplazamiento dentro del tubo
};

const int MAX_PART_VASO = 450;
std::vector<Vaso> vasos;
std::vector<ParticulaVaso> particulasVaso;

glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

std::vector<glm::vec3> muestrearRuta(const std::vector<glm::vec3>& cp, int porSegmento) {
    std::vector<glm::vec3> out;
    int n = (int)cp.size();
    for (int i = 0; i < n - 1; ++i) {
        glm::vec3 p0 = cp[std::max(i - 1, 0)];
        glm::vec3 p1 = cp[i];
        glm::vec3 p2 = cp[i + 1];
        glm::vec3 p3 = cp[std::min(i + 2, n - 1)];
        for (int s = 0; s < porSegmento; ++s) {
            out.push_back(catmullRom(p0, p1, p2, p3, (float)s / (float)porSegmento));
        }
    }
    out.push_back(cp[n - 1]);
    return out;
}

void crearVasos() {
    vasos.clear();

    // Aorta (roja): sale de la parte superior, hace un arco y baja por detras
    {
        Vaso v;
        std::vector<glm::vec3> cp = {
            glm::vec3(0.12f, 0.85f, 0.05f), glm::vec3(0.12f, 1.30f, 0.05f),
            glm::vec3(0.05f, 1.72f, 0.00f), glm::vec3(-0.30f, 1.95f, -0.05f),
            glm::vec3(-0.68f, 1.75f, -0.10f), glm::vec3(-0.80f, 1.25f, -0.15f),
            glm::vec3(-0.60f, 0.70f, -0.20f)
        };
        v.ruta = muestrearRuta(cp, 24);
        v.color = glm::vec3(0.90f, 0.10f, 0.12f);
        v.radio = 0.14f;
        vasos.push_back(v);
    }
    // Vena cava (azul)
    {
        Vaso v;
        std::vector<glm::vec3> cp = {
            glm::vec3(0.40f, 0.60f, 0.25f), glm::vec3(0.55f, 1.00f, 0.20f),
            glm::vec3(0.62f, 1.45f, 0.12f), glm::vec3(0.62f, 1.75f, 0.08f),
            glm::vec3(0.58f, 2.05f, 0.04f)
        };
        v.ruta = muestrearRuta(cp, 24);
        v.color = glm::vec3(0.15f, 0.28f, 0.90f);
        v.radio = 0.10f;
        vasos.push_back(v);
    }
    // Arteria pulmonar (violeta)
    {
        Vaso v;
        std::vector<glm::vec3> cp = {
            glm::vec3(0.00f, 0.70f, 0.40f), glm::vec3(0.12f, 1.15f, 0.48f),
            glm::vec3(0.15f, 1.45f, 0.52f), glm::vec3(-0.10f, 1.68f, 0.46f),
            glm::vec3(-0.48f, 1.72f, 0.32f)
        };
        v.ruta = muestrearRuta(cp, 24);
        v.color = glm::vec3(0.55f, 0.22f, 0.80f);
        v.radio = 0.10f;
        vasos.push_back(v);
    }
}

void agregarTubo(std::vector<float>& v, std::vector<unsigned int>& idx, const Vaso& vaso, int lados) {
    const std::vector<glm::vec3>& r = vaso.ruta;
    int n = (int)r.size();
    unsigned int base = (unsigned int)(v.size() / 11);

    glm::vec3 T0 = glm::normalize(r[1] - r[0]);
    glm::vec3 ref = (fabsf(T0.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 N = glm::normalize(glm::cross(T0, ref));

    for (int i = 0; i < n; ++i) {
        glm::vec3 T = glm::normalize(r[std::min(i + 1, n - 1)] - r[std::max(i - 1, 0)]);
        glm::vec3 Nn = N - T * glm::dot(N, T);
        if (glm::length(Nn) > 1e-4f) N = glm::normalize(Nn);
        glm::vec3 B = glm::cross(T, N);

        float u = (float)i / (float)(n - 1);
        float cap = std::min(u, 1.0f - u) / 0.04f;
        if (cap > 1.0f) cap = 1.0f;
        float rad = vaso.radio * (0.88f + 0.12f * sinf(u * PI_F)) * sqrtf(cap);

        for (int j = 0; j <= lados; ++j) {
            float a = 2.0f * PI_F * (float)j / (float)lados;
            glm::vec3 dir = N * cosf(a) + B * sinf(a);
            glm::vec3 pos = r[i] + dir * rad;
            v.push_back(pos.x); v.push_back(pos.y); v.push_back(pos.z);
            v.push_back(dir.x); v.push_back(dir.y); v.push_back(dir.z);
            v.push_back(u); v.push_back((float)j / (float)lados);
            v.push_back(vaso.color.x); v.push_back(vaso.color.y); v.push_back(vaso.color.z);
        }
    }

    for (int i = 0; i < n - 1; ++i) {
        for (int j = 0; j < lados; ++j) {
            unsigned int a = base + i * (lados + 1) + j;
            unsigned int b = a + lados + 1;
            idx.push_back(a); idx.push_back(b); idx.push_back(a + 1);
            idx.push_back(a + 1); idx.push_back(b); idx.push_back(b + 1);
        }
    }
}

void inicializarParticulasVaso() {
    particulasVaso.resize(MAX_PART_VASO);
    for (int i = 0; i < MAX_PART_VASO; ++i) {
        ParticulaVaso& p = particulasVaso[i];
        p.vaso = rand() % (int)vasos.size();
        p.t = (rand() % 1000) / 1000.0f;
        p.vel = 0.18f + ((rand() % 100) / 100.0f) * 0.20f;
        float rr = vasos[p.vaso].radio * 0.55f;
        p.jitter = glm::vec3(((rand() % 100) / 100.0f - 0.5f), ((rand() % 100) / 100.0f - 0.5f), ((rand() % 100) / 100.0f - 0.5f)) * rr;
    }
}

glm::vec3 puntoEnVaso(const Vaso& v, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float f = t * (float)(v.ruta.size() - 1);
    int i0 = (int)f;
    if (i0 >= (int)v.ruta.size() - 1) i0 = (int)v.ruta.size() - 2;
    float k = f - (float)i0;
    return glm::mix(v.ruta[i0], v.ruta[i0 + 1], k);
}

void actualizarParticulasVaso(float dt, float bpm, float beat, int patologia) {
    float f = (bpm / 60.0f) * (0.75f + 0.8f * beat);
    if (patologia == 3) f *= (0.4f + (rand() % 100) / 100.0f);
    for (auto& p : particulasVaso) {
        p.t += p.vel * dt * f;
        if (p.t > 1.0f) {
            p.t -= 1.0f;
            p.vaso = rand() % (int)vasos.size();
            float rr = vasos[p.vaso].radio * 0.55f;
            p.jitter = glm::vec3(((rand() % 100) / 100.0f - 0.5f), ((rand() % 100) / 100.0f - 0.5f), ((rand() % 100) / 100.0f - 0.5f)) * rr;
        }
    }
}

// ---------------------------------------------------------------------
//  Callbacks de mouse (originales)
// ---------------------------------------------------------------------
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            haciendoArrastre = true;
            glfwGetCursorPos(window, &lastX, &lastY);
        }
        else if (action == GLFW_RELEASE) {
            haciendoArrastre = false;
        }
    }
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (!haciendoArrastre) return;

    float xoffset = (float)(xpos - lastX);
    float yoffset = (float)(lastY - ypos);
    lastX = xpos;
    lastY = ypos;

    float sensibilidad = 0.4f;
    targetYaw += xoffset * sensibilidad;
    targetPitch += yoffset * sensibilidad;

    if (targetPitch > 89.0f) targetPitch = 89.0f;
    if (targetPitch < -89.0f) targetPitch = -89.0f;
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    targetDistancia -= (float)yoffset * 0.3f;
    if (targetDistancia < 1.5f) targetDistancia = 1.5f;
    if (targetDistancia > 10.0f) targetDistancia = 10.0f;
}

// ---------------------------------------------------------------------
//  Utilidades de shaders
// ---------------------------------------------------------------------
std::string leerShaderCode(const char* filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return "";
    std::stringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

GLuint crearShaderProgram(const char* vertPath, const char* fragPath) {
    std::string vertCode = leerShaderCode(vertPath);
    std::string fragCode = leerShaderCode(fragPath);

    if (vertCode.empty() || fragCode.empty()) return 0;

    const char* vSource = vertCode.c_str();
    const char* fSource = fragCode.c_str();

    GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader, 1, &vSource, NULL);
    glCompileShader(vertShader);

    GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragShader, 1, &fSource, NULL);
    glCompileShader(fragShader);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);

    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    return program;
}

// compila un programa desde codigo fuente embebido y muestra errores en consola
GLuint compilarEtapa(GLenum tipo, const char* src, const char* nombre) {
    GLuint s = glCreateShader(tipo);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, 1024, NULL, log);
        std::cerr << "[Shader " << nombre << "] error de compilacion:\n" << log << std::endl;
    }
    return s;
}

GLuint crearProgramaSrc(const char* vsSrc, const char* fsSrc, const char* nombre) {
    GLuint vs = compilarEtapa(GL_VERTEX_SHADER, vsSrc, nombre);
    GLuint fs = compilarEtapa(GL_FRAGMENT_SHADER, fsSrc, nombre);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, 1024, NULL, log);
        std::cerr << "[Programa " << nombre << "] error de enlace:\n" << log << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

inline GLint U(GLuint prog, const char* name) { return glGetUniformLocation(prog, name); }

GLuint cargarTextura(const char* path) {
    GLuint textureID = 0;
    int width, height, nrComponents;
    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(path, &width, &height, &nrComponents, STBI_rgb_alpha);

    if (data) {
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    return textureID;
}

// ---------------------------------------------------------------------
//  Geometria del corazon
// ---------------------------------------------------------------------
void generarGeometriaCorazon(std::vector<float>& vertices, std::vector<unsigned int>& indices, int anillos, int sectores) {
    const float PI = 3.14159265359f;

    for (int i = 0; i <= anillos; ++i) {
        float stackAngle = PI / 2.0f - i * (PI / anillos);
        float v = (float)i / anillos;

        float sinStack = sinf(stackAngle);
        float cosStack = cosf(stackAngle);

        for (int j = 0; j <= sectores; ++j) {
            float sectorAngle = j * (2.0f * PI / sectores);
            float u = (float)j / sectores;

            float x = cosStack * cosf(sectorAngle);
            float y = sinStack;
            float z = cosStack * sinf(sectorAngle);

            y *= 1.25f;

            float factorApex = (y < 0.0f) ? (1.0f + 0.6f * y) : 1.0f;
            if (factorApex < 0.18f) factorApex = 0.18f;
            x *= factorApex;
            z *= factorApex;

            x += 0.12f * (1.0f - y) * (1.0f - y);

            float hendiduraSurco = 1.0f - 0.10f * expf(-powf((sectorAngle - 1.2f) / 0.4f, 2.0f));
            x *= hendiduraSurco;
            z *= hendiduraSurco;

            glm::vec3 norm = glm::normalize(glm::vec3(x, y, z));

            vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
            vertices.push_back(norm.x); vertices.push_back(norm.y); vertices.push_back(norm.z);
            vertices.push_back(u * 3.0f);
            vertices.push_back(v * 2.0f);
        }
    }

    for (int i = 0; i < anillos; ++i) {
        int k1 = i * (sectores + 1);
        int k2 = k1 + sectores + 1;

        for (int j = 0; j < sectores; ++j, ++k1, ++k2) {
            if (i != 0) {
                indices.push_back(k1); indices.push_back(k2); indices.push_back(k1 + 1);
            }
            if (i != (anillos - 1)) {
                indices.push_back(k1 + 1); indices.push_back(k2); indices.push_back(k2 + 1);
            }
        }
    }
}

float calcularVoltajeECG(float tiempo, float bpm, int patologia) {
    float periodo = 60.0f / bpm;
    float t = fmod(tiempo, periodo) / periodo;

    if (patologia == 3) {
        t += 0.05f * sin(tiempo * 25.0f);
    }

    float p = 0.15f * expf(-powf((t - 0.2f) / 0.03f, 2.0f));
    float q = -0.15f * expf(-powf((t - 0.42f) / 0.015f, 2.0f));
    float r = 1.20f * expf(-powf((t - 0.45f) / 0.012f, 2.0f));
    float s = -0.25f * expf(-powf((t - 0.48f) / 0.015f, 2.0f));
    float t_wave = 0.30f * expf(-powf((t - 0.7f) / 0.06f, 2.0f));

    return p + q + r + s + t_wave;
}

// =====================================================================
// SHADERS EMBEBIDOS PARA LOS EFECTOS
// =====================================================================

// ---- Triangulo a pantalla completa (fondo y post-procesado) ----
static const char* VS_FULLSCREEN = R"GLSL(
#version 330 core
out vec2 uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// ---- Fondo: degradado + niebla animada que late con el corazon ----
static const char* FS_FONDO = R"GLSL(
#version 330 core
in vec2 uv;
out vec4 FragColor;
uniform float time;
uniform float beat;
uniform float aspect;
uniform vec3 accent;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * noise(p);
        p *= 2.02;
        a *= 0.5;
    }
    return v;
}
void main() {
    vec2 c = (uv - 0.5) * vec2(aspect, 1.0);
    float r = length(c);
    vec3 top = vec3(0.012, 0.020, 0.050);
    vec3 bot = vec3(0.035, 0.010, 0.025);
    vec3 col = mix(bot, top, uv.y);
    float haze = fbm(c * 2.5 + vec2(time * 0.03, -time * 0.02));
    col += accent * 0.06 * haze * (1.0 - smoothstep(0.0, 0.95, r));
    col += accent * (0.05 + 0.10 * beat) * exp(-r * r * 5.0);
    col += vec3(0.8, 0.05, 0.08) * 0.06 * beat * exp(-r * r * 2.0);
    FragColor = vec4(col, 1.0);
}
)GLSL";

// ---- Piso holografico con grilla y ondas expansivas en cada latido ----
static const char* VS_PISO = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 view;
uniform mat4 projection;
out vec3 wp;
void main() {
    wp = aPos;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
)GLSL";

static const char* FS_PISO = R"GLSL(
#version 330 core
in vec3 wp;
out vec4 FragColor;
uniform float time;
uniform float beatAge;
uniform float beat;
uniform vec3 accent;

float gridLine(vec2 c, float scale) {
    vec2 q = c * scale;
    vec2 g = abs(fract(q - 0.5) - 0.5) / max(fwidth(q), vec2(0.0001));
    return 1.0 - min(min(g.x, g.y), 1.0);
}
void main() {
    vec2 c = wp.xz;
    float r = length(c);
    float g = gridLine(c, 1.0) * 0.6 + gridLine(c, 0.2) * 0.4;
    float fade = exp(-r * 0.32);
    float rr = beatAge * 3.5;
    float ring = exp(-pow((r - rr) / 0.15, 2.0)) * exp(-beatAge * 1.2);
    float ring2 = exp(-pow((r - rr + 0.7) / 0.10, 2.0)) * exp(-beatAge * 1.5) * 0.5;
    float pool = exp(-r * r * 0.9) * (0.18 + 0.35 * beat);
    float inten = g * fade * 0.35 + ring * 1.2 + ring2 + pool;
    inten += g * ring * 1.5;
    FragColor = vec4(accent * inten, 1.0);
}
)GLSL";

// ---- Halo / aura que pulsa detras del corazon (billboard) ----
static const char* VS_HALO = R"GLSL(
#version 330 core
uniform mat4 view;
uniform mat4 projection;
uniform vec3 center;
uniform vec3 camRight;
uniform vec3 camUp;
uniform float size;
out vec2 q;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
    q = corner;
    vec3 p = center + (camRight * corner.x + camUp * corner.y) * size;
    gl_Position = projection * view * vec4(p, 1.0);
}
)GLSL";

static const char* FS_HALO = R"GLSL(
#version 330 core
in vec2 q;
out vec4 FragColor;
uniform vec3 accent;
uniform float intensity;
uniform float beatAge;
void main() {
    float d = length(q);
    float g = exp(-d * d * 4.0);
    float ringR = 0.42 + 0.5 * (1.0 - exp(-beatAge * 3.0));
    float ring = exp(-pow((d - ringR) / 0.07, 2.0)) * exp(-beatAge * 2.5) * 0.6;
    float a = (g * 0.5 + ring) * (1.0 - smoothstep(0.7, 1.0, d)) * intensity;
    FragColor = vec4(accent * a, 1.0);
}
)GLSL";

// ---- Vasos sanguineos: tubos translucidos con flujo animado ----
static const char* VS_VASO = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aColor;
uniform mat4 view;
uniform mat4 projection;
uniform float beat;
out vec3 wp;
out vec3 N;
out vec2 uv;
out vec3 col;
void main() {
    N = normalize(aNormal);
    wp = aPos + N * beat * 0.020;
    uv = aUV;
    col = aColor;
    gl_Position = projection * view * vec4(wp, 1.0);
}
)GLSL";

static const char* FS_VASO = R"GLSL(
#version 330 core
in vec3 wp;
in vec3 N;
in vec2 uv;
in vec3 col;
out vec4 FragColor;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform float tiempo;
uniform float alpha;
uniform float flowSpeed;
uniform float beat;
void main() {
    vec3 n = normalize(N);
    vec3 V = normalize(viewPos - wp);
    vec3 L = normalize(lightPos - wp);
    vec3 H = normalize(L + V);
    float diff = max(dot(n, L), 0.0);
    float spec = pow(max(dot(n, H), 0.0), 64.0) * 0.6;
    float fres = pow(1.0 - max(dot(n, V), 0.0), 3.0);
    float flow = smoothstep(0.55, 1.0, sin(uv.x * 40.0 - tiempo * flowSpeed * 3.0));
    vec3 c = col * (0.25 + 0.85 * diff) + vec3(spec) + fres * col * 0.9 + col * flow * (0.25 + 0.4 * beat);
    FragColor = vec4(c, clamp(alpha + fres * 0.3, 0.0, 1.0));
}
)GLSL";

// ---- Particulas suaves con brillo (sangre, flujo y polvo ambiental) ----
static const char* VS_PARTICULAS = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aData;
layout(location = 2) in vec3 aColor;
uniform mat4 view;
uniform mat4 projection;
uniform float pointScale;
uniform float baseSize;
uniform float time;
uniform float beat;
uniform int dust;
out float vFade;
out vec3 vColor;
void main() {
    vec3 p = aPos;
    float fade = aData.y;
    vec3 colr = aColor;
    if (dust == 1) {
        float sp = 0.03 + 0.07 * aData.x;
        float m = mod(aPos.y + time * sp, 6.0);
        p.y = m - 2.5;
        p.x += sin(time * 0.25 + aPos.z * 6.0) * 0.25;
        p.z += cos(time * 0.21 + aPos.x * 5.0) * 0.25;
        fade = (0.35 + 0.65 * (0.5 + 0.5 * sin(time * (0.8 + aData.y * 1.7) + aData.y * 40.0)));
        fade *= smoothstep(0.0, 0.6, m) * smoothstep(0.0, 0.6, 6.0 - m);
        colr = vec3(1.0);
    }
    vec4 vp = view * vec4(p, 1.0);
    gl_Position = projection * vp;
    float pulso = (dust == 1) ? 1.0 : (0.85 + 0.5 * beat);
    gl_PointSize = clamp(baseSize * pointScale / max(-vp.z, 0.1) * pulso, 1.0, 64.0);
    vFade = fade;
    vColor = colr;
}
)GLSL";

static const char* FS_PARTICULAS = R"GLSL(
#version 330 core
in float vFade;
in vec3 vColor;
out vec4 FragColor;
uniform int dust;
uniform vec3 accent;
uniform float beat;
void main() {
    vec2 pc = gl_PointCoord * 2.0 - 1.0;
    float d = dot(pc, pc);
    if (d > 1.0) discard;
    float core = exp(-d * 5.0);
    float halo = exp(-d * 1.6) * 0.5;
    vec3 c;
    if (dust == 1) {
        c = (accent * 0.9 + vec3(0.1)) * (core + halo * 0.6) * vFade * 0.55;
    } else {
        c = vColor * (core * 1.3 + halo) * vFade * (1.0 + beat * 0.6);
    }
    FragColor = vec4(c, 1.0);
}
)GLSL";

// ---- Rim holografico (Fresnel), malla y escaner sobre el corazon ----
static const char* VS_RIM = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 view;
uniform mat4 projection;
uniform float expand;
out vec3 N;
out vec3 wp;
void main() {
    N = normalize(aNormal);
    wp = aPos + N * expand;
    gl_Position = projection * view * vec4(wp, 1.0);
}
)GLSL";

static const char* FS_RIM = R"GLSL(
#version 330 core
in vec3 N;
in vec3 wp;
out vec4 FragColor;
uniform vec3 viewPos;
uniform vec3 accent;
uniform float intensity;
uniform float time;
uniform float scanY;
uniform float scanOn;
uniform float power;
uniform int wire;
void main() {
    vec3 V = normalize(viewPos - wp);
    float ndv = abs(dot(normalize(N), V));
    float f = pow(1.0 - ndv, power);
    float scan = scanOn * exp(-pow((wp.y - scanY) / 0.045, 2.0));
    float lines = 0.5 + 0.5 * sin(wp.y * 60.0 - time * 2.0);
    vec3 c;
    if (wire == 1) {
        c = accent * (0.20 + 1.2 * scan + 0.5 * f) * intensity;
    } else {
        c = accent * (f * intensity * (0.75 + 0.25 * lines) + scan * 1.4);
    }
    FragColor = vec4(c, 1.0);
}
)GLSL";

// ---- Post-procesado: bloom, aberracion cromatica, vineta, grano, glitch ----
static const char* FS_POST = R"GLSL(
#version 330 core
in vec2 uv;
out vec4 FragColor;
uniform sampler2D scene;
uniform vec2 texel;
uniform float time;
uniform float bloomInt;
uniform float bloomThr;
uniform float vignette;
uniform float aberr;
uniform float grain;
uniform float beat;
uniform float glitch;
uniform int scan;
uniform vec3 tint;

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

vec3 brightPass(vec3 c) {
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float k = max(l - bloomThr, 0.0) / max(l, 0.0001);
    return c * k;
}

vec3 bloomAt(vec2 st) {
    vec3 b = vec3(0.0);
    float lods[4] = float[4](2.0, 3.0, 4.0, 5.0);
    float w[4] = float[4](0.35, 0.30, 0.20, 0.15);
    for (int i = 0; i < 4; i++) {
        float lod = lods[i];
        vec2 o = texel * exp2(lod) * 0.75;
        vec3 s = brightPass(textureLod(scene, st, lod).rgb) * 2.0;
        s += brightPass(textureLod(scene, st + vec2(o.x, 0.0), lod).rgb);
        s += brightPass(textureLod(scene, st - vec2(o.x, 0.0), lod).rgb);
        s += brightPass(textureLod(scene, st + vec2(0.0, o.y), lod).rgb);
        s += brightPass(textureLod(scene, st - vec2(0.0, o.y), lod).rgb);
        b += (s / 6.0) * w[i];
    }
    return b;
}

void main() {
    vec2 st = uv;

    if (glitch > 0.0) {
        float line = floor(st.y * 48.0);
        float hh = hash(vec2(line, floor(time * 14.0)));
        if (hh > 0.82) st.x += (hash(vec2(line, time)) - 0.5) * 0.05 * glitch;
    }

    vec2 dir = st - 0.5;
    float dist = length(dir);
    float ab = aberr * (0.0015 + 0.0035 * beat + 0.004 * glitch) * (0.4 + dist * 2.0);

    vec3 col;
    col.r = texture(scene, st + dir * ab * 4.0).r;
    col.g = texture(scene, st).g;
    col.b = texture(scene, st - dir * ab * 4.0).b;

    col += bloomAt(st) * bloomInt * (1.0 + 0.35 * beat);
    col *= tint;

    float vig = (1.0 - smoothstep(0.25, 0.95, dist * 1.15));
    col *= mix(1.0, vig, vignette);

    // Hombro suave para que los brillos no se quemen de golpe
    vec3 comp = 0.8 + 0.2 * tanh((col - 0.8) / 0.2);
    col = mix(col, comp, step(vec3(0.8), col));

    if (scan == 1) {
        float py = uv.y / texel.y;
        col *= 1.0 - 0.08 * step(1.5, mod(py, 3.0));
    }

    col += (hash(uv / texel + time) - 0.5) * grain;
    FragColor = vec4(col, 1.0);
}
)GLSL";

// =====================================================================
//  NUEVO: Framebuffer HDR multisample para el post-procesado
// =====================================================================
struct PostFX {
    GLuint msFBO = 0, msColor = 0, msDepth = 0;
    GLuint resFBO = 0, resTex = 0;
    int w = 0, h = 0;
    bool ok = false;
};

void liberarPostFX(PostFX& f) {
    if (f.msFBO) glDeleteFramebuffers(1, &f.msFBO);
    if (f.msColor) glDeleteRenderbuffers(1, &f.msColor);
    if (f.msDepth) glDeleteRenderbuffers(1, &f.msDepth);
    if (f.resFBO) glDeleteFramebuffers(1, &f.resFBO);
    if (f.resTex) glDeleteTextures(1, &f.resTex);
    f.msFBO = f.msColor = f.msDepth = f.resFBO = f.resTex = 0;
    f.ok = false;
}

bool crearPostFX(PostFX& f, int w, int h) {
    liberarPostFX(f);
    f.w = w;
    f.h = h;

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    int samples = (maxSamples >= 4) ? 4 : (int)maxSamples;

    glGenFramebuffers(1, &f.msFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, f.msFBO);

    glGenRenderbuffers(1, &f.msColor);
    glBindRenderbuffer(GL_RENDERBUFFER, f.msColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, f.msColor);

    glGenRenderbuffers(1, &f.msDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, f.msDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, f.msDepth);

    bool okMS = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glGenTextures(1, &f.resTex);
    glBindTexture(GL_TEXTURE_2D, f.resTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &f.resFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, f.resFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f.resTex, 0);
    bool okRes = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    f.ok = okMS && okRes;
    if (!f.ok) {
        std::cerr << "[PostFX] No se pudo crear el framebuffer HDR, se desactiva el post-procesado." << std::endl;
        liberarPostFX(f);
    }
    return f.ok;
}

// =====================================================================
//  NUEVO: Estilo visual de la interfaz y monitor HUD
// =====================================================================
void aplicarEstiloMedico() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 8.0f;
    s.FrameRounding = 5.0f;
    s.GrabRounding = 5.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.ItemSpacing = ImVec2(8.0f, 7.0f);
    s.WindowPadding = ImVec2(12.0f, 12.0f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.055f, 0.095f, 0.90f);
    c[ImGuiCol_Border] = ImVec4(0.10f, 0.55f, 0.80f, 0.45f);
    c[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.10f, 0.18f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.05f, 0.20f, 0.32f, 1.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.13f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.24f, 0.36f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.14f, 0.32f, 0.48f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.15f, 0.75f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.90f, 1.00f, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(0.25f, 0.90f, 1.00f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.08f, 0.32f, 0.50f, 0.85f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.50f, 0.75f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.65f, 0.95f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.08f, 0.30f, 0.46f, 0.80f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.12f, 0.45f, 0.68f, 0.90f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.58f, 0.85f, 1.00f);
    c[ImGuiCol_Separator] = ImVec4(0.10f, 0.45f, 0.65f, 0.50f);
    c[ImGuiCol_PlotLines] = ImVec4(0.00f, 1.00f, 0.40f, 1.00f);
}

// Monitor tipo UCI dibujado sobre la escena (FC, signos vitales y ECG con barrido)
void dibujarMonitorHUD(const float* ecg, int n, int off, float bpm, int pat, float beat,
    const float acc[3], float t, bool pausado) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 disp = ImGui::GetIO().DisplaySize;

    const float W = 430.0f, H = 200.0f;
    ImVec2 p0(disp.x - W - 14.0f, disp.y - H - 14.0f);
    ImVec2 p1(p0.x + W, p0.y + H);

    ImU32 colAcc = IM_COL32((int)(acc[0] * 255), (int)(acc[1] * 255), (int)(acc[2] * 255), 255);
    ImU32 colAccSoft = IM_COL32((int)(acc[0] * 255), (int)(acc[1] * 255), (int)(acc[2] * 255), 70);

    dl->AddRectFilled(p0, p1, IM_COL32(4, 9, 16, 195), 10.0f);
    dl->AddRect(p0, p1, colAccSoft, 10.0f, 0, 2.0f);

    // Zona del ECG
    float ex0 = p0.x + 15.0f;
    float eyTop = p0.y + 85.0f;
    float eyBot = p0.y + H - 15.0f;
    for (float gx = 0.0f; gx <= 400.0f; gx += 20.0f) {
        dl->AddLine(ImVec2(ex0 + gx, eyTop), ImVec2(ex0 + gx, eyBot), IM_COL32(40, 90, 120, (((int)gx) % 100 == 0) ? 70 : 32));
    }
    for (float gy = eyTop; gy <= eyBot + 0.5f; gy += 20.0f) {
        dl->AddLine(ImVec2(ex0, gy), ImVec2(ex0 + 400.0f, gy), IM_COL32(40, 90, 120, 32));
    }

    // Trazo con barrido (la cabeza de escritura deja un hueco que avanza)
    int head = (off - 1 + n) % n;
    for (int i = 0; i < n - 1; ++i) {
        int dist = (i - head + n) % n;
        if (dist < 10) continue;
        float v0 = ecg[i];
        float v1 = ecg[i + 1];
        float y0 = eyBot - (v0 + 0.5f) / 2.0f * (eyBot - eyTop);
        float y1 = eyBot - (v1 + 0.5f) / 2.0f * (eyBot - eyTop);
        ImVec2 a(ex0 + (float)i, y0);
        ImVec2 b(ex0 + (float)i + 1.0f, y1);
        dl->AddLine(a, b, IM_COL32(0, 255, 100, 55), 4.0f);
        dl->AddLine(a, b, IM_COL32(120, 255, 170, 255), 1.6f);
    }

    // Icono de corazon que late
    float s = 1.0f + 0.30f * beat;
    float cx = p0.x + 32.0f, cy = p0.y + 36.0f;
    ImU32 colHeart = IM_COL32(255, 50, 70, 255);
    dl->AddCircleFilled(ImVec2(cx - 5.5f * s, cy - 3.0f * s), 7.5f * s, colHeart);
    dl->AddCircleFilled(ImVec2(cx + 5.5f * s, cy - 3.0f * s), 7.5f * s, colHeart);
    dl->AddTriangleFilled(ImVec2(cx - 12.3f * s, cy - 0.5f * s), ImVec2(cx + 12.3f * s, cy - 0.5f * s), ImVec2(cx, cy + 14.0f * s), colHeart);

    // Frecuencia cardiaca en grande
    char buf[64];
    ImFont* font = ImGui::GetFont();
    float fs = ImGui::GetFontSize();
    snprintf(buf, sizeof(buf), "%d", (int)(bpm + 0.5f));
    dl->AddText(font, fs * 3.0f, ImVec2(p0.x + 62.0f, p0.y + 12.0f), colAcc, buf);
    dl->AddText(font, fs * 0.9f, ImVec2(p0.x + 62.0f, p0.y + 62.0f), IM_COL32(150, 190, 210, 255), "FC (lpm)");

    // Signos vitales simulados (solo ilustrativos)
    int sist = 120, dia = 80, spo2 = 98;
    float vs = 70.0f;
    if (pat == 1) { sist = 102; dia = 66; spo2 = 96; vs = 45.0f; }
    else if (pat == 2) { sist = 108; dia = 72; spo2 = 97; vs = 90.0f; }
    else if (pat == 3) { sist = 128 + (int)(6.0f * sinf(t * 3.0f)); dia = 84; spo2 = 94; vs = 60.0f; }
    float gc = bpm * vs / 1000.0f;

    float tx = p0.x + 215.0f;
    snprintf(buf, sizeof(buf), "PA   %d/%d mmHg", sist, dia);
    dl->AddText(ImVec2(tx, p0.y + 14.0f), IM_COL32(230, 235, 240, 255), buf);
    snprintf(buf, sizeof(buf), "SpO2 %d %%", spo2);
    dl->AddText(ImVec2(tx, p0.y + 34.0f), IM_COL32(120, 220, 255, 255), buf);
    snprintf(buf, sizeof(buf), "GC   %.1f L/min", gc);
    dl->AddText(ImVec2(tx, p0.y + 54.0f), IM_COL32(255, 210, 120, 255), buf);
    dl->AddText(ImVec2(tx, p0.y + 70.0f), IM_COL32(120, 140, 155, 255), "(valores simulados)");

    if (pausado) {
        dl->AddText(font, fs * 1.4f, ImVec2(p1.x - 95.0f, p0.y + 10.0f), IM_COL32(255, 200, 0, 255), "|| PAUSA");
    }
}

// =====================================================================
//  MAIN
// =====================================================================
int main() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Simulador Cardiaco Quirurgico 3D", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return -1;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_PROGRAM_POINT_SIZE);

    // ---- Shaders y texturas ORIGINALES (rutas sin cambios) ----
    GLuint shaderProgram = crearShaderProgram("assets/shaders/corazon.vert", "assets/shaders/corazon.frag");
    GLuint texturaTejido = cargarTextura("assets/textures/corazon_color.jpg");
    GLuint texturaNormales = cargarTextura("assets/textures/corazon_normal.jpg");
    GLuint texturaRoughness = cargarTextura("assets/textures/corazon_roughness.jpg");

    // ---- NUEVO: shaders embebidos ----
    GLuint progFondo = crearProgramaSrc(VS_FULLSCREEN, FS_FONDO, "fondo");
    GLuint progPost = crearProgramaSrc(VS_FULLSCREEN, FS_POST, "post");
    GLuint progPiso = crearProgramaSrc(VS_PISO, FS_PISO, "piso");
    GLuint progHalo = crearProgramaSrc(VS_HALO, FS_HALO, "halo");
    GLuint progVaso = crearProgramaSrc(VS_VASO, FS_VASO, "vaso");
    GLuint progPart = crearProgramaSrc(VS_PARTICULAS, FS_PARTICULAS, "particulas");
    GLuint progRim = crearProgramaSrc(VS_RIM, FS_RIM, "rim");

    // VAO vacio para dibujar triangulos/quads generados en el vertex shader
    GLuint vaoVacio;
    glGenVertexArrays(1, &vaoVacio);

    // ---- Malla del corazon (original) ----
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    generarGeometriaCorazon(vertices, indices, 64, 64);

    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // ---- NUEVO: malla de vasos sanguineos ----
    crearVasos();
    std::vector<float> vasoVerts;
    std::vector<unsigned int> vasoIdx;
    for (const auto& v : vasos) agregarTubo(vasoVerts, vasoIdx, v, 20);

    GLuint vasoVAO, vasoVBO, vasoEBO;
    glGenVertexArrays(1, &vasoVAO);
    glGenBuffers(1, &vasoVBO);
    glGenBuffers(1, &vasoEBO);
    glBindVertexArray(vasoVAO);
    glBindBuffer(GL_ARRAY_BUFFER, vasoVBO);
    glBufferData(GL_ARRAY_BUFFER, vasoVerts.size() * sizeof(float), vasoVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vasoEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, vasoIdx.size() * sizeof(unsigned int), vasoIdx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    GLsizei vasoIndexCount = (GLsizei)vasoIdx.size();

    // ---- NUEVO: piso (quad grande) ----
    float pisoY = -1.60f;
    float pisoS = 9.0f;
    float pisoVerts[] = {
        -pisoS, pisoY, -pisoS,
         pisoS, pisoY, -pisoS,
         pisoS, pisoY,  pisoS,
        -pisoS, pisoY, -pisoS,
         pisoS, pisoY,  pisoS,
        -pisoS, pisoY,  pisoS
    };
    GLuint pisoVAO, pisoVBO;
    glGenVertexArrays(1, &pisoVAO);
    glGenBuffers(1, &pisoVBO);
    glBindVertexArray(pisoVAO);
    glBindBuffer(GL_ARRAY_BUFFER, pisoVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(pisoVerts), pisoVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // ---- Particulas: buffer dinamico (sangre + flujo en vasos) ----
    inicializarParticulas();
    inicializarParticulasVaso();

    GLuint particulaVAO, particulaVBO;
    glGenVertexArrays(1, &particulaVAO);
    glGenBuffers(1, &particulaVBO);
    glBindVertexArray(particulaVAO);
    glBindBuffer(GL_ARRAY_BUFFER, particulaVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // ---- NUEVO: polvo ambiental (animado totalmente en GPU) ----
    const int NUM_POLVO = 500;
    std::vector<float> polvoData;
    polvoData.reserve(NUM_POLVO * 8);
    for (int i = 0; i < NUM_POLVO; ++i) {
        polvoData.push_back(((rand() % 1000) / 1000.0f - 0.5f) * 8.0f);
        polvoData.push_back(((rand() % 1000) / 1000.0f) * 6.0f);
        polvoData.push_back(((rand() % 1000) / 1000.0f - 0.5f) * 8.0f);
        polvoData.push_back((rand() % 1000) / 1000.0f);
        polvoData.push_back((rand() % 1000) / 1000.0f);
        polvoData.push_back(1.0f); polvoData.push_back(1.0f); polvoData.push_back(1.0f);
    }
    GLuint polvoVAO, polvoVBO;
    glGenVertexArrays(1, &polvoVAO);
    glGenBuffers(1, &polvoVBO);
    glBindVertexArray(polvoVAO);
    glBindBuffer(GL_ARRAY_BUFFER, polvoVBO);
    glBufferData(GL_ARRAY_BUFFER, polvoData.size() * sizeof(float), polvoData.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // ---- ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    aplicarEstiloMedico();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ---- Estado de la simulacion ----
    float bpm = 75.0f;
    int patologia = 0;
    int patologiaPrev = 0;
    bool modoRayosX = false;
    bool simulacionPausada = false;
    bool modoMapaEstres = false;
    bool luzMovilCamara = true;
    bool usarTextura = (texturaTejido != 0);
    bool usarNormalMap = (texturaNormales != 0);
    bool usarRoughness = (texturaRoughness != 0);
    bool mostrarParticulas = true;
    float colorTejido[3] = { 0.72f, 0.08f, 0.12f };

    // ---- NUEVO: opciones de efectos ----
    bool efectosPost = true;
    float bloomInt = 0.65f;
    float bloomThr = 0.55f;
    float vineta = 0.75f;
    float aberracion = 1.0f;
    float grano = 0.025f;
    bool lineasEscaneo = false;
    bool mostrarFondo = true;
    bool mostrarPiso = true;
    bool mostrarHalo = true;
    float haloIntensidad = 1.0f;
    bool mostrarVasos = true;
    bool mostrarPolvo = true;
    bool mostrarEstelas = true;
    bool mostrarRim = true;
    float rimIntensidad = 1.4f;
    bool mostrarEscaner = true;
    bool mostrarMalla = false;
    bool mostrarMonitorHUD = true;
    bool autoOrbita = false;
    float velOrbita = 10.0f;
    float sacudidaCamara = 0.25f;
    float fovBase = 45.0f;
    float dollyLatido = 1.0f;
    bool mostrarPanel = true;

    // Color de acento (cambia segun la patologia, con transicion suave)
    float accent[3] = { 0.10f, 0.75f, 1.00f };

    // ---- ECG con muestreo fijo (100 muestras/seg, independiente de los FPS) ----
    const int ECG_N = 400;
    float ecgBuffer[ECG_N] = { 0 };
    int ecgOffset = 0;
    float ecgAcc = 0.0f;
    float tiempoAcumulado = 0.0f;
    float lastFrameTime = 0.0f;

    PostFX postfx;
    bool postDisponible = true;

    // Estado de teclas (atajos)
    bool kPrev[8] = { false };

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - lastFrameTime;
        lastFrameTime = currentFrame;
        if (deltaTime > 0.1f) deltaTime = 0.1f;

        // ---- NUEVO: atajos de teclado ----
        auto tecla = [&](int k, int slot) -> bool {
            bool now = (glfwGetKey(window, k) == GLFW_PRESS);
            bool trig = now && !kPrev[slot];
            kPrev[slot] = now;
            return trig && !io.WantCaptureKeyboard;
            };
        if (tecla(GLFW_KEY_SPACE, 0)) simulacionPausada = !simulacionPausada;
        if (tecla(GLFW_KEY_R, 1)) { targetYaw = 0.0f; targetPitch = 0.0f; targetDistancia = 3.5f; }
        if (tecla(GLFW_KEY_H, 2)) mostrarPanel = !mostrarPanel;
        if (tecla(GLFW_KEY_X, 3)) modoRayosX = !modoRayosX;
        if (tecla(GLFW_KEY_F, 4)) efectosPost = !efectosPost;
        if (tecla(GLFW_KEY_O, 5)) autoOrbita = !autoOrbita;
        if (tecla(GLFW_KEY_M, 6)) mostrarMalla = !mostrarMalla;
        if (tecla(GLFW_KEY_E, 7)) modoMapaEstres = !modoMapaEstres;

        // Orbita automatica (se detiene mientras arrastras con el mouse)
        if (autoOrbita && !haciendoArrastre) targetYaw += velOrbita * deltaTime;

        // Suavizado de movimiento de camara (Lerp)
        camYaw += (targetYaw - camYaw) * 0.1f;
        camPitch += (targetPitch - camPitch) * 0.1f;
        camDistancia += (targetDistancia - camDistancia) * 0.1f;

        if (!simulacionPausada) {
            tiempoAcumulado += deltaTime;
            actualizarParticulas(deltaTime, bpm, patologia);
        }

        // Al cambiar de patologia se aplica su BPM tipico; luego el slider se puede ajustar libremente
        if (patologia != patologiaPrev) {
            if (patologia == 0) bpm = 75.0f;
            else if (patologia == 1) bpm = 140.0f;
            else if (patologia == 2) bpm = 45.0f;
            patologiaPrev = patologia;
        }

        // Muestreo del ECG a tasa fija
        if (!simulacionPausada) {
            ecgAcc += deltaTime;
            while (ecgAcc >= 0.01f) {
                ecgAcc -= 0.01f;
                float ts = tiempoAcumulado - ecgAcc;
                ecgBuffer[ecgOffset] = calcularVoltajeECG(ts, bpm, patologia);
                ecgOffset = (ecgOffset + 1) % ECG_N;
            }
        }

        float periodo = 60.0f / bpm;
        float faseCiclo = fmod(tiempoAcumulado, periodo) / periodo;
        bool esSistole = (faseCiclo > 0.38f && faseCiclo < 0.52f);

        // ---- NUEVO: envolvente del latido (para sincronizar todos los efectos) ----
        float beatAge = fmodf(tiempoAcumulado - 0.45f * periodo, periodo);
        if (beatAge < 0.0f) beatAge += periodo;
        float beat = expf(-beatAge * 8.0f) + 0.5f * expf(-powf((beatAge - 0.30f) / 0.08f, 2.0f));
        if (beat > 1.0f) beat = 1.0f;

        // Efecto glitch para la arritmia (rafagas aleatorias)
        float glitch = 0.0f;
        if (patologia == 3) {
            float hh = fabsf(fmodf(sinf(floorf(tiempoAcumulado * 6.0f) * 91.3f) * 43758.5f, 1.0f));
            glitch = (hh > 0.90f) ? 1.0f : 0.0f;
        }

        // Color de acento segun patologia (transicion suave)
        float objetivo[3] = { 0.10f, 0.75f, 1.00f };
        if (patologia == 1) { objetivo[0] = 1.00f; objetivo[1] = 0.25f; objetivo[2] = 0.15f; }
        else if (patologia == 2) { objetivo[0] = 1.00f; objetivo[1] = 0.70f; objetivo[2] = 0.10f; }
        else if (patologia == 3) { objetivo[0] = 1.00f; objetivo[1] = 0.10f; objetivo[2] = 0.55f; }
        float kAcc = std::min(1.0f, deltaTime * 4.0f);
        for (int i = 0; i < 3; ++i) accent[i] += (objetivo[i] - accent[i]) * kAcc;

        if (!simulacionPausada) {
            actualizarParticulasVaso(deltaTime, bpm, beat, patologia);
        }

        // =================================================================
        //  Interfaz
        // =================================================================
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (mostrarPanel) {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(390, 700), ImGuiCond_FirstUseEver);

            ImGui::Begin("SIMULADOR CLINICO & FISIOLOGICO");
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Plataforma de Entrenamiento Medico");
            ImGui::Separator();

            const char* listaPatologias[] = { "Ritmo Sinusal (Normal)", "Taquicardia Ventricular", "Bradicardia Sinusal", "Arritmia Cardiacas" };
            ImGui::Combo("Patologia", &patologia, listaPatologias, IM_ARRAYSIZE(listaPatologias));

            if (patologia == 0) ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.3f, 1.0f), "ESTADO: RITMO SINUSAL NORMAL");
            else if (patologia == 1) ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "ALERTA: TAQUICARDIA (>100 BPM)");
            else if (patologia == 2) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "ALERTA: BRADICARDIA (<60 BPM)");
            else ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.5f, 1.0f), "CRITICO: ARRITMIA / EXTRASISTOLE");

            // Indicador de Latido / Pulso Sonoro-Visual
            if (esSistole) {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.1f, 1.0f), "=== [ PULSO: LUB-DUB (SISTOLE) ] ===");
            }
            else {
                ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "    [ Relajacion (Diastole) ]    ");
            }

            // NUEVO: barra de pulso sincronizada con el latido
            ImGui::ProgressBar(beat, ImVec2(-1.0f, 6.0f), "");

            ImGui::SliderFloat("Frecuencia (BPM)", &bpm, 30.0f, 200.0f);

            if (ImGui::Button(simulacionPausada ? " Reanudar Simulacion " : " Pausar Simulacion ")) {
                simulacionPausada = !simulacionPausada;
            }

            ImGui::Separator();
            ImGui::Text("PRESETS DE CAMARA MEDICA:");
            if (ImGui::Button(" Vista General ")) { targetYaw = 0.0f; targetPitch = 0.0f; targetDistancia = 3.5f; }
            ImGui::SameLine();
            if (ImGui::Button(" Enfoque Apex ")) { targetYaw = 25.0f; targetPitch = -35.0f; targetDistancia = 2.2f; }
            ImGui::SameLine();
            if (ImGui::Button(" Surco Coronario ")) { targetYaw = -65.0f; targetPitch = 10.0f; targetDistancia = 2.4f; }
            // NUEVO: mas presets
            if (ImGui::Button(" Vasos Superiores ")) { targetYaw = 20.0f; targetPitch = 40.0f; targetDistancia = 3.0f; }
            ImGui::SameLine();
            if (ImGui::Button(" Vista Posterior ")) { targetYaw = 180.0f; targetPitch = 10.0f; targetDistancia = 3.5f; }
            ImGui::SameLine();
            if (ImGui::Button(" Cinematica ")) { autoOrbita = true; targetPitch = 12.0f; targetDistancia = 3.8f; }

            ImGui::Separator();
            ImGui::Text("MONITOR ECG (ELECTROCARDIOGRAMA)");
            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 1.0f, 0.4f, 1.0f));
            ImGui::PlotLines("##ECG", ecgBuffer, ECG_N, ecgOffset, "mV", -0.5f, 1.5f, ImVec2(0, 80));
            ImGui::PopStyleColor();

            ImGui::Separator();
            ImGui::Text("HERRAMIENTAS DIAGNOSTICAS:");
            ImGui::Checkbox("Mapa de Estres Miocardico (Tension)", &modoMapaEstres);
            ImGui::Checkbox("Luz Quirurgica Orbital", &luzMovilCamara);
            ImGui::Checkbox("Modo Transparencia / Rayos X", &modoRayosX);
            ImGui::Checkbox("Simular Flujo Sanguineo", &mostrarParticulas);

            ImGui::Separator();
            ImGui::Checkbox("Textura Tejido (PBR)", &usarTextura);
            ImGui::Checkbox("Mapa Relieve (Normales)", &usarNormalMap);
            ImGui::Checkbox("Asperosidad (Roughness)", &usarRoughness);
            ImGui::ColorEdit3("Color del tejido", colorTejido);

            // ---------------- NUEVO: panel de efectos visuales ----------------
            ImGui::Separator();
            if (ImGui::CollapsingHeader("EFECTOS VISUALES", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Post-procesado (Bloom / Vineta) [F]", &efectosPost);
                if (efectosPost) {
                    ImGui::SliderFloat("Bloom (brillo)", &bloomInt, 0.0f, 2.0f);
                    ImGui::SliderFloat("Bloom umbral", &bloomThr, 0.1f, 1.2f);
                    ImGui::SliderFloat("Vineta", &vineta, 0.0f, 1.0f);
                    ImGui::SliderFloat("Aberracion cromatica", &aberracion, 0.0f, 3.0f);
                    ImGui::SliderFloat("Grano de pelicula", &grano, 0.0f, 0.12f);
                    ImGui::Checkbox("Lineas de escaneo (estilo monitor)", &lineasEscaneo);
                }
                ImGui::Checkbox("Aura / Halo pulsante", &mostrarHalo);
                if (mostrarHalo) ImGui::SliderFloat("Intensidad aura", &haloIntensidad, 0.0f, 2.5f);
                ImGui::Checkbox("Contorno holografico (Fresnel)", &mostrarRim);
                if (mostrarRim) ImGui::SliderFloat("Intensidad contorno", &rimIntensidad, 0.0f, 4.0f);
                ImGui::Checkbox("Escaner holografico", &mostrarEscaner);
                ImGui::Checkbox("Malla / Wireframe [M]", &mostrarMalla);
                ImGui::Checkbox("Vasos sanguineos con flujo", &mostrarVasos);
                ImGui::Checkbox("Estelas en las particulas", &mostrarEstelas);
                ImGui::Checkbox("Polvo ambiental", &mostrarPolvo);
                ImGui::Checkbox("Piso holografico + ondas", &mostrarPiso);
                ImGui::Checkbox("Fondo animado", &mostrarFondo);
                ImGui::Checkbox("Monitor HUD en pantalla", &mostrarMonitorHUD);
            }
            if (ImGui::CollapsingHeader("CAMARA")) {
                ImGui::Checkbox("Orbita automatica [O]", &autoOrbita);
                ImGui::SliderFloat("Velocidad orbita", &velOrbita, 1.0f, 60.0f);
                ImGui::SliderFloat("Sacudida con latido", &sacudidaCamara, 0.0f, 1.5f);
                ImGui::SliderFloat("Campo de vision (FOV)", &fovBase, 25.0f, 80.0f);
                ImGui::SliderFloat("Dolly con latido", &dollyLatido, 0.0f, 3.0f);
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.8f, 1.0f), "Atajos: Espacio=Pausa  R=Reset cam  H=Ocultar panel");
            ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.8f, 1.0f), "X=Rayos X  F=Post-FX  O=Orbita  M=Malla  E=Estres");

            ImGui::Separator();
            ImGui::Text("FPS: %.1f | Autor: Leandro Zamora", io.Framerate);
            ImGui::End();
        }

        if (mostrarMonitorHUD) {
            dibujarMonitorHUD(ecgBuffer, ECG_N, ecgOffset, bpm, patologia, beat, accent, tiempoAcumulado, simulacionPausada);
        }

        ImGui::Render();

        // =================================================================
        //  Render 3D
        // =================================================================
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        if (display_w <= 0 || display_h <= 0) {
            glfwSwapBuffers(window);
            continue;
        }

        // Crear / recrear el framebuffer HDR si cambia el tamano de la ventana
        if (efectosPost && postDisponible) {
            if (!postfx.ok || postfx.w != display_w || postfx.h != display_h) {
                if (!crearPostFX(postfx, display_w, display_h)) postDisponible = false;
            }
        }
        bool usarPost = efectosPost && postDisponible && postfx.ok;

        if (usarPost) glBindFramebuffer(GL_FRAMEBUFFER, postfx.msFBO);
        else glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glViewport(0, 0, display_w, display_h);
        glDepthMask(GL_TRUE);
        glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---- Camara (con sacudida y "dolly" sutil en cada latido) ----
        float fov = fovBase - 1.5f * beat * dollyLatido;
        float camX = camDistancia * cos(glm::radians(camPitch)) * sin(glm::radians(camYaw));
        float camY = camDistancia * sin(glm::radians(camPitch));
        float camZ = camDistancia * cos(glm::radians(camPitch)) * cos(glm::radians(camYaw));
        glm::vec3 cameraPos = glm::vec3(camX, camY, camZ);
        glm::vec3 shake = glm::vec3(sinf(currentFrame * 53.0f), cosf(currentFrame * 47.0f), sinf(currentFrame * 61.0f)) * (0.012f * sacudidaCamara * beat);
        cameraPos += shake;

        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(fov), (float)display_w / (float)display_h, 0.1f, 100.0f);

        glm::vec3 camRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
        glm::vec3 camUp = glm::vec3(view[0][1], view[1][1], view[2][1]);
        glm::vec3 camFwd = glm::normalize(-cameraPos);

        // Iluminacion dinamica: la luz sigue a la camara para generar reflejos espectaculares
        glm::vec3 lPos = luzMovilCamara ? (cameraPos + glm::vec3(1.0f, 2.0f, 1.0f)) : glm::vec3(2.5f, 4.0f, 3.5f);

        // ---------------- 1) Fondo animado ----------------
        if (mostrarFondo) {
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glUseProgram(progFondo);
            glUniform1f(U(progFondo, "time"), tiempoAcumulado);
            glUniform1f(U(progFondo, "beat"), beat);
            glUniform1f(U(progFondo, "aspect"), (float)display_w / (float)display_h);
            glUniform3f(U(progFondo, "accent"), accent[0], accent[1], accent[2]);
            glBindVertexArray(vaoVacio);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // ---------------- 2) Halo + piso (aditivos, antes del corazon) ----------------
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);

        if (mostrarHalo) {
            glUseProgram(progHalo);
            glUniformMatrix4fv(U(progHalo, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(U(progHalo, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glm::vec3 haloCentro = camFwd * 0.9f + glm::vec3(0.0f, 0.1f, 0.0f);
            glUniform3f(U(progHalo, "center"), haloCentro.x, haloCentro.y, haloCentro.z);
            glUniform3f(U(progHalo, "camRight"), camRight.x, camRight.y, camRight.z);
            glUniform3f(U(progHalo, "camUp"), camUp.x, camUp.y, camUp.z);
            glUniform1f(U(progHalo, "size"), 2.8f);
            glUniform3f(U(progHalo, "accent"), accent[0], accent[1], accent[2]);
            glUniform1f(U(progHalo, "intensity"), haloIntensidad * (0.45f + 0.9f * beat));
            glUniform1f(U(progHalo, "beatAge"), beatAge);
            glBindVertexArray(vaoVacio);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        if (mostrarPiso) {
            glUseProgram(progPiso);
            glUniformMatrix4fv(U(progPiso, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(U(progPiso, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1f(U(progPiso, "time"), tiempoAcumulado);
            glUniform1f(U(progPiso, "beatAge"), beatAge);
            glUniform1f(U(progPiso, "beat"), beat);
            glUniform3f(U(progPiso, "accent"), accent[0], accent[1], accent[2]);
            glBindVertexArray(pisoVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // ---------------- 3) Corazon (shader y texturas ORIGINALES) ----------------
        glUseProgram(shaderProgram);

        if (texturaTejido != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texturaTejido);
            glUniform1i(glGetUniformLocation(shaderProgram, "texturaTejido"), 0);
        }
        if (texturaNormales != 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, texturaNormales);
            glUniform1i(glGetUniformLocation(shaderProgram, "texturaNormales"), 1);
        }
        if (texturaRoughness != 0) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, texturaRoughness);
            glUniform1i(glGetUniformLocation(shaderProgram, "texturaRoughness"), 2);
        }

        glUniform1i(glGetUniformLocation(shaderProgram, "usarTextura"), usarTextura && (texturaTejido != 0));
        glUniform1i(glGetUniformLocation(shaderProgram, "usarNormalMap"), usarNormalMap && (texturaNormales != 0));
        glUniform1i(glGetUniformLocation(shaderProgram, "usarRoughness"), usarRoughness && (texturaRoughness != 0));
        glUniform1i(glGetUniformLocation(shaderProgram, "modoMapaEstres"), modoMapaEstres);

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        glUniform1f(glGetUniformLocation(shaderProgram, "tiempo"), tiempoAcumulado);
        glUniform1f(glGetUniformLocation(shaderProgram, "bpm"), bpm);
        glUniform1f(glGetUniformLocation(shaderProgram, "transparencia"), modoRayosX ? 0.35f : 1.0f);

        glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), lPos.x, lPos.y, lPos.z);
        glUniform3f(glGetUniformLocation(shaderProgram, "lightColor"), 1.0f, 0.98f, 0.95f);
        glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), colorTejido[0], colorTejido[1], colorTejido[2]);

        // En modo Rayos X no se escribe profundidad para poder ver vasos y particulas a traves del corazon
        if (modoRayosX) glDepthMask(GL_FALSE);
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);

        // ---------------- 4) Vasos sanguineos translucidos ----------------
        if (mostrarVasos) {
            glDepthMask(GL_FALSE);
            glUseProgram(progVaso);
            glUniformMatrix4fv(U(progVaso, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(U(progVaso, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3f(U(progVaso, "lightPos"), lPos.x, lPos.y, lPos.z);
            glUniform3f(U(progVaso, "viewPos"), cameraPos.x, cameraPos.y, cameraPos.z);
            glUniform1f(U(progVaso, "tiempo"), tiempoAcumulado);
            glUniform1f(U(progVaso, "alpha"), modoRayosX ? 0.30f : 0.50f);
            glUniform1f(U(progVaso, "flowSpeed"), bpm / 60.0f);
            glUniform1f(U(progVaso, "beat"), beat);
            glBindVertexArray(vasoVAO);
            glDrawElements(GL_TRIANGLES, vasoIndexCount, GL_UNSIGNED_INT, 0);
            glDepthMask(GL_TRUE);
        }

        // ---------------- 5) Particulas de sangre con brillo y estelas ----------------
        if (mostrarParticulas) {
            std::vector<float> pb;
            pb.reserve((MAX_PARTICULAS * 4 + MAX_PART_VASO * 5) * 8);

            auto push = [&](const glm::vec3& p, float d0, float d1, const glm::vec3& c) {
                pb.push_back(p.x); pb.push_back(p.y); pb.push_back(p.z);
                pb.push_back(d0); pb.push_back(d1);
                pb.push_back(c.x); pb.push_back(c.y); pb.push_back(c.z);
                };

            int trail = mostrarEstelas ? 4 : 1;

            // Sangre interior (rojo brillante al nacer -> carmesi oscuro al morir)
            for (const auto& p : particulas) {
                float ratio = p.vida / p.vidaMax;
                if (ratio < 0.0f) ratio = 0.0f;
                if (ratio > 1.0f) ratio = 1.0f;
                glm::vec3 c = glm::mix(glm::vec3(0.55f, 0.0f, 0.03f), glm::vec3(1.0f, 0.50f, 0.18f), ratio);
                float fadeVida = std::min(1.0f, ratio * 6.0f);
                glm::vec3 dir = glm::normalize(p.velocidad);
                for (int k = 0; k < trail; ++k) {
                    float f = 1.0f - (float)k / (float)trail;
                    push(p.posicion - dir * (0.05f * (float)k), ratio, f * f * fadeVida, c);
                }
            }

            // Flujo dentro de los vasos
            for (const auto& p : particulasVaso) {
                const Vaso& vs = vasos[p.vaso];
                glm::vec3 c = glm::mix(vs.color, glm::vec3(1.0f), 0.35f);
                float fadeT = sinf(p.t * PI_F);
                for (int k = 0; k < trail; ++k) {
                    float f = 1.0f - (float)k / (float)trail;
                    glm::vec3 pos = puntoEnVaso(vs, p.t - 0.012f * (float)k) + p.jitter;
                    push(pos, 1.0f, f * f * fadeT, c);
                }
            }

            glUseProgram(progPart);
            glUniformMatrix4fv(U(progPart, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(U(progPart, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            float pointScale = (float)display_h / (2.0f * tanf(glm::radians(fov) * 0.5f));
            glUniform1f(U(progPart, "pointScale"), pointScale);
            glUniform1f(U(progPart, "baseSize"), 0.040f);
            glUniform1f(U(progPart, "time"), tiempoAcumulado);
            glUniform1f(U(progPart, "beat"), beat);
            glUniform1i(U(progPart, "dust"), 0);
            glUniform3f(U(progPart, "accent"), accent[0], accent[1], accent[2]);

            glBindVertexArray(particulaVAO);
            glBindBuffer(GL_ARRAY_BUFFER, particulaVBO);
            glBufferData(GL_ARRAY_BUFFER, pb.size() * sizeof(float), pb.data(), GL_DYNAMIC_DRAW);

            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glDrawArrays(GL_POINTS, 0, (GLsizei)(pb.size() / 8));
            glDepthMask(GL_TRUE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // ---------------- 6) Contorno holografico, escaner y malla ----------------
        if (mostrarRim || mostrarEscaner || mostrarMalla) {
            glUseProgram(progRim);
            glUniformMatrix4fv(U(progRim, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(U(progRim, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3f(U(progRim, "viewPos"), cameraPos.x, cameraPos.y, cameraPos.z);
            glUniform3f(U(progRim, "accent"), accent[0], accent[1], accent[2]);
            glUniform1f(U(progRim, "time"), tiempoAcumulado);
            float scanY = -1.35f + fmodf(tiempoAcumulado * 0.35f, 1.0f) * 2.9f;
            glUniform1f(U(progRim, "scanY"), scanY);
            glUniform1f(U(progRim, "scanOn"), mostrarEscaner ? 1.0f : 0.0f);

            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glBindVertexArray(VAO);

            if (mostrarRim || mostrarEscaner) {
                glDisable(GL_DEPTH_TEST);
                float inten = mostrarRim ? (rimIntensidad * (modoRayosX ? 1.6f : 1.0f) * (0.7f + 0.6f * beat)) : 0.0f;
                glUniform1f(U(progRim, "intensity"), inten);
                glUniform1f(U(progRim, "power"), 3.0f);
                glUniform1f(U(progRim, "expand"), 0.01f);
                glUniform1i(U(progRim, "wire"), 0);
                glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);
                glEnable(GL_DEPTH_TEST);
            }

            if (mostrarMalla) {
                glDepthFunc(GL_LEQUAL);
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glUniform1f(U(progRim, "intensity"), 0.9f);
                glUniform1f(U(progRim, "power"), 2.0f);
                glUniform1f(U(progRim, "expand"), 0.02f);
                glUniform1i(U(progRim, "wire"), 1);
                glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glDepthFunc(GL_LESS);
            }

            glDepthMask(GL_TRUE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // ---------------- 7) Polvo ambiental ----------------
        if (mostrarPolvo) {
            glUseProgram(progPart);
            glUniformMatrix4fv(U(progPart, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(U(progPart, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            float pointScale = (float)display_h / (2.0f * tanf(glm::radians(fov) * 0.5f));
            glUniform1f(U(progPart, "pointScale"), pointScale);
            glUniform1f(U(progPart, "baseSize"), 0.015f);
            glUniform1f(U(progPart, "time"), tiempoAcumulado);
            glUniform1f(U(progPart, "beat"), beat);
            glUniform1i(U(progPart, "dust"), 1);
            glUniform3f(U(progPart, "accent"), accent[0], accent[1], accent[2]);

            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glBindVertexArray(polvoVAO);
            glDrawArrays(GL_POINTS, 0, NUM_POLVO);
            glDepthMask(GL_TRUE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // ---------------- 8) Post-procesado (resolve MSAA + bloom + efectos) ----------------
        if (usarPost) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, postfx.msFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postfx.resFBO);
            glBlitFramebuffer(0, 0, display_w, display_h, 0, 0, display_w, display_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, display_w, display_h);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, postfx.resTex);
            glGenerateMipmap(GL_TEXTURE_2D);

            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);

            glUseProgram(progPost);
            glUniform1i(U(progPost, "scene"), 0);
            glUniform2f(U(progPost, "texel"), 1.0f / (float)display_w, 1.0f / (float)display_h);
            glUniform1f(U(progPost, "time"), tiempoAcumulado);
            glUniform1f(U(progPost, "bloomInt"), bloomInt);
            glUniform1f(U(progPost, "bloomThr"), bloomThr);
            glUniform1f(U(progPost, "vignette"), vineta);
            glUniform1f(U(progPost, "aberr"), aberracion);
            glUniform1f(U(progPost, "grain"), grano);
            glUniform1f(U(progPost, "beat"), beat);
            glUniform1f(U(progPost, "glitch"), glitch);
            glUniform1i(U(progPost, "scan"), lineasEscaneo ? 1 : 0);
            glUniform3f(U(progPost, "tint"),
                1.0f + (accent[0] - 0.5f) * 0.10f,
                1.0f + (accent[1] - 0.5f) * 0.10f,
                1.0f + (accent[2] - 0.5f) * 0.10f);
            glBindVertexArray(vaoVacio);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // ---- Limpieza ----
    liberarPostFX(postfx);
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteVertexArrays(1, &vasoVAO);
    glDeleteBuffers(1, &vasoVBO);
    glDeleteBuffers(1, &vasoEBO);
    glDeleteVertexArrays(1, &pisoVAO);
    glDeleteBuffers(1, &pisoVBO);
    glDeleteVertexArrays(1, &polvoVAO);
    glDeleteBuffers(1, &polvoVBO);
    glDeleteVertexArrays(1, &vaoVacio);
    glDeleteVertexArrays(1, &particulaVAO);
    glDeleteBuffers(1, &particulaVBO);
    glDeleteProgram(shaderProgram);
    glDeleteProgram(progFondo);
    glDeleteProgram(progPost);
    glDeleteProgram(progPiso);
    glDeleteProgram(progHalo);
    glDeleteProgram(progVaso);
    glDeleteProgram(progPart);
    glDeleteProgram(progRim);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
