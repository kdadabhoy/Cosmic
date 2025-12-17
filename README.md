<div align="center">

# Airplane Optimizer and Simulator w/ GUI! 

## by Kaden Dadabhoy

</div>



<br>

# Introduction / Methodology

Hello. This is a refactored and was more expansive version of the to-9km-and-beyond project (https://github.com/kdadabhoy/to-9km-and-beyond). A lot of the airplane logic from that project is used in this project... However that project was OOP based (very reable, very modular, and easier to develop)... This project is meant to be more like a game. Well it will have:


    1. A main screen where you can configure an airplane 
    2. A screen where you can see the simulation of that airplane
    3. A screen where you can run optimization 
    4. A screen that is a game-like version where you can fly that airplane you configed
        * You should also be able to export data from this


In order to accomplish this... which is a lot more resource intensive... an entity component system (esc) approach was taken. Well... not exactly, but the core principles are there. This apporach allows multi-threading and basically enables this program to run like a game... It should also decrease optimization times! More on this later.


## External Libraries / Dependencies
1. OpenGL
1. Dear ImGUI
1. GLAD
1. GLFW



<br>
<br>




## Entity Component System (ESC) Approach... and my (semi-rip off) version of it!
1. A





<br>
<br>



# File Tree of Project
Put a pic of the file tree here sometime
<br>
<br>



# Notes (Updated as Developed... Should give a general overview)
This section is more of my own personal notes on implementation stuff... we will see if it stays in the final version of this readme

<br>



## General Approach