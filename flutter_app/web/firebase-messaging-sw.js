// Background service worker for Firebase Cloud Messaging on web.
//
// MUST live at the web root (served as /firebase-messaging-sw.js) for the
// browser to register it. Flutter's web/ directory is served as the root,
// so this file lands in the right place automatically.
//
// The OS handles display of background pushes — this SW is required for
// the browser to wake up and forward messages even when the tab is closed.
// Foreground messages are handled by lib/services/fcm_service.dart.

importScripts('https://www.gstatic.com/firebasejs/10.13.0/firebase-app-compat.js');
importScripts('https://www.gstatic.com/firebasejs/10.13.0/firebase-messaging-compat.js');

firebase.initializeApp({
  apiKey: 'AIzaSyABxr5DlWydIXW7EBHk7nYh2r3qlf4df_I',
  authDomain: 'llm-tutor-d721e.firebaseapp.com',
  projectId: 'llm-tutor-d721e',
  storageBucket: 'llm-tutor-d721e.firebasestorage.app',
  messagingSenderId: '233818904977',
  appId: '1:233818904977:web:76839d8d4143d89d616230',
});

firebase.messaging();
