import pygame
from pygame.locals import *
from OpenGL.GL import *
from OpenGL.GLUT import *
from OpenGL.GLU import *

# Initialize Pygame
pygame.init()
display = (800, 600)
pygame.display.set_mode(display, DOUBLEBUF | OPENGL)

# Initialize OpenGL
gluPerspective(45, (display[0] / display[1]), 0.1, 50.0)
glTranslatef(0.0, 0.0, -5)


def square():
    glBegin(GL_QUADS)
    glVertex3f(1, 1, 0)
    glVertex3f(-1, 1, 0)
    glVertex3f(-1, -1, 0)
    glVertex3f(1, -1, 0)
    glEnd()


def main():
    while True:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit()
                quit()

        glRotatef(1, 3, 1, 1)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        square()
        pygame.display.flip()
        pygame.time.wait(10)


main()
