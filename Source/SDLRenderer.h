//
//  SDLRenderer.h
//  GEngine
//
//  Created by Clark Kromenaker on 7/22/17.
//
#pragma once

#include "SDL/SDL.h"
#include <GL/glew.h>
#include "GLVertexArray.h"
#include "GLShader.h"

class Model;

class SDLRenderer
{
public:
    bool Initialize();
    void Shutdown();
    
    void Clear();
    void Render();
    void Present();
    
    void SetModel(Model* model);
    
private:
    // Handle for the window object (contains the game).
    SDL_Window* mWindow = nullptr;
    
    // Context for rendering in OpenGL.
    SDL_GLContext mContext;
    
    // Compiled default shader program.
    GLuint mBasicMeshProgram = GL_NONE;
    
    GLShader* mShader = nullptr;
    
    Model* mModel = nullptr;
    GLVertexArray* mVertArray = nullptr;
};
