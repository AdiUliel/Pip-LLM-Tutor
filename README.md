## XXXXX Project by :  
  
## Details about the project
 
## Folder description :
* ESP32: source code for the esp side (firmware).
* Documentation: wiring diagram + basic operating instructions
* Unit Tests: tests for individual hardware components (input / output devices)
* flutter_app : dart code for our Flutter app.
* Parameters: contains description of parameters and settings that can be modified IN YOUR CODE
* Assets: link to 3D printed parts, Audio files used in this project, Fritzing file for connection diagram (FZZ format) etc

## ESP32 SDK version used in this project: 
* ESP32 by Espressif - version 2.0.17

## Arduino/ESP32 libraries used in this project:
* ESP32 Speech-to-Text - version 2.0.1
* XXXX - version XXXXX
* XXXX - version XXXXX

## Connection diagram:
* [View the ESP32-S3 Hardware Documentation Here](Documentation/Hardware%20Documentation/index.html)
## Project Poster:
 
This project is part of ICST - The Interdisciplinary Center for Smart Technologies, Taub Faculty of Computer Science, Technion
https://icst.cs.technion.ac.il/

## LLM Tutor Interface

The project now includes a Firebase Functions based LLM tutor layer.
The device writes the child's speech transcript to `sessions/{sessionId}/exchanges/{exchangeId}` and the Cloud Function returns structured feedback, emotion, and the next question.

See: `Documentation/LLM_INTERFACE.md`.

Main files:

- `firebase/functions/index.js`
- `firebase/functions/tutorEngine.js`
- `firebase/functions/questionGenerator.js`
- `firebase/firestore.rules`

