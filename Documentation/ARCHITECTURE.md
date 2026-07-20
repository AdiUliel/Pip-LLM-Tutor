# System Architecture — Pip LLM Tutor

End-to-end data flow across the three parts of the system. The ESP32 device talks
only to Firebase (never directly to the AI APIs); the Cloud Functions call Google
Cloud AI services using the **project service account (ADC)** — there are no LLM
API keys stored anywhere.

```mermaid
flowchart TB
    subgraph Device["🧸 ESP32-S3 device"]
        ESP[Firmware<br/>mic · speaker · display · button]
    end

    subgraph Cloud["☁️ Firebase / Google Cloud"]
        CF[Cloud Functions<br/>processTurn · tutor engine<br/>extractQuestions · triggers]
        FS[(Firestore<br/>sessions · questions · children<br/>materials · deviceState · pairingCodes)]
        ST[(Storage<br/>homework files · TTS cache)]
        subgraph AI["Google Cloud AI APIs — via service account, no keys"]
            STT[Speech-to-Text]
            GEM[Gemini 2.5 Flash-Lite<br/>Vertex AI]
            TTS[Text-to-Speech he-IL]
        end
    end

    subgraph App["📱 Flutter parent app — Android / Web"]
        FL[Setup · materials · reports · device monitor]
    end

    ESP -- "raw PCM audio + sessionId (processTurn)" --> CF
    CF -- "result + inline TTS MP3" --> ESP
    ESP -- "deviceState heartbeat + anon auth (direct REST)" --> FS

    CF --> STT
    CF --> GEM
    CF --> TTS
    CF -- "session / questions / stats" --> FS
    CF -- "read materials · cache TTS" --> ST

    FL -- "read reports / device status" --> FS
    FL -- "write child · pairing" --> FS
    FL -- "upload homework" --> ST
    CF -. "FCM push (lesson start/end, offline)" .-> FL
```

## Legend

- **Device → Cloud Functions:** one `processTurn` HTTPS call per answer — uploads
  the raw PCM audio, receives the graded feedback + the spoken MP3 inline
  (see [PERFORMANCE_EVALUATION.md](PERFORMANCE_EVALUATION.md)).
- **Device → Firestore (direct):** the device also writes its `deviceState`
  heartbeat and creates sessions over the Firestore REST API (anonymous auth), so
  the app sees it live.
- **Cloud Functions → AI:** STT transcribes, Gemini (Vertex AI) grades + generates
  feedback/questions, TTS synthesizes speech — all with the project service
  account, **no API keys**.
- **App ↔ Firestore/Storage:** the parent app reads reports/device status and
  writes children, pairing and uploaded materials; homework files go to Storage.
- **FCM:** best-effort push notifications (lesson start/end, device offline).
