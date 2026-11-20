#pragma once

#include <GLES2/gl2.h>
#include <string>

namespace kiosk::gl {

// Compiles and links the single shared vertex/fragment shader pair used for
// every draw in this app (solid rects, thumbnails, text): a textured quad
// whose sampled color is multiplied by a per-vertex color. Solid rects bind
// a 1x1 white texture; text binds the font atlas (white RGB, coverage in A).
// One program, one vertex layout -- avoids compiling a shader per draw call.
class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    bool build();
    void destroy();

    GLuint program() const { return program_; }
    GLint attribPos() const { return attribPos_; }
    GLint attribUv() const { return attribUv_; }
    GLint attribColor() const { return attribColor_; }
    GLint uniformProjection() const { return uniformProjection_; }
    GLint uniformTexture() const { return uniformTexture_; }

private:
    GLuint program_ = 0;
    GLint attribPos_ = -1;
    GLint attribUv_ = -1;
    GLint attribColor_ = -1;
    GLint uniformProjection_ = -1;
    GLint uniformTexture_ = -1;
};

GLuint compileShader(GLenum type, const char* source);
GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);

} // namespace kiosk::gl
