#pragma once

#include <GL/glew.h>

class GLSLProgram
{
public:
	GLSLProgram(void);
	~GLSLProgram(void);

	void CompileShaders(const char* vertexShaderFilePath, const char* fragmentShaderFilePath);

	void LinkShader();

	void AddAttribute(const char* attribName);

	void Use();
	void UnUse();
private:
	void compileShader(const char* filePath, GLuint id);

	int numAtrtrib;
	GLuint programId;

	GLuint vertexShaderId;
	GLuint fragmentShaderId;
};

