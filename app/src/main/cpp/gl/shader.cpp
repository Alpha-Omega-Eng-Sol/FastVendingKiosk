#include "shader.h"

#include "gl_common.h"

namespace kiosk::gl {

namespace {

const char* kVertexSrc = R"GLSL(
attribute vec2 aPos;
attribute vec2 aUv;
attribute vec4 aColor;
uniform mat4 uProjection;
varying vec2 vUv;
varying vec4 vColor;
void main() {
    vUv = aUv;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)GLSL";

const char* kFragmentSrc = R"GLSL(
precision mediump float;
varying vec2 vUv;
varying vec4 vColor;
uniform sampler2D uTexture;
void main() {
    vec4 texel = texture2D(uTexture, vUv);
    gl_FragColor = texel * vColor;
}
)GLSL";

} // namespace

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<size_t>(logLen), '\0');
        if (logLen > 0) glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        LOGE("Shader compile failed: %s", log.c_str());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<size_t>(logLen), '\0');
        if (logLen > 0) glGetProgramInfoLog(program, logLen, nullptr, log.data());
        LOGE("Program link failed: %s", log.c_str());
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

ShaderProgram::~ShaderProgram() {
    destroy();
}

bool ShaderProgram::build() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexSrc);
    if (!vs) return false;
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentSrc);
    if (!fs) { glDeleteShader(vs); return false; }

    program_ = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!program_) return false;

    attribPos_ = glGetAttribLocation(program_, "aPos");
    attribUv_ = glGetAttribLocation(program_, "aUv");
    attribColor_ = glGetAttribLocation(program_, "aColor");
    uniformProjection_ = glGetUniformLocation(program_, "uProjection");
    uniformTexture_ = glGetUniformLocation(program_, "uTexture");
    return true;
}

void ShaderProgram::destroy() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

} // namespace kiosk::gl
