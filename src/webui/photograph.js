const photographButton = document.querySelector('#photograph-button');
const photographCanvas = document.querySelector('#photograph');
const photographContext = photographCanvas.getContext('2d');
const photographStatus = document.querySelector('#photograph-status');
const gestureName = document.querySelector('#gesture-name');
const saveGestureButton = document.querySelector('#save-gesture-button');
const saveGestureStatus = document.querySelector('#save-gesture-status');
const savedGesturesElement = document.querySelector('#saved-gestures');
const storageKey = 'gestured.gestures';
let photographedVectors = [];

function getPalmCenter(landmarks) {
    const palmLandmarks = [landmarks[0], landmarks[5], landmarks[9], landmarks[13], landmarks[17]];
    const palmX = palmLandmarks.reduce((sum, point) => sum + point[0], 0) / 5;
    const palmY = palmLandmarks.reduce((sum, point) => sum + point[1], 0) / 5;
    return [palmX, palmY];
}

function normalizeHand(landmarks) {
    const wrist = landmarks[0];
    const middleFinger = landmarks[9];
    const palmCenter = getPalmCenter(landmarks);
    const palmSize = Math.hypot(middleFinger[0] - wrist[0], middleFinger[1] - wrist[1]);

    return landmarks.flatMap(point => [
        (point[0] - palmCenter[0]) / palmSize,
        (point[1] - palmCenter[1]) / palmSize
    ]);
}

function getPhotographPoints(vector) {
    const points = [];
    for (let index = 0; index < vector.length; index += 2) {
        points.push([0.5 + vector[index] * 0.22, 0.5 + vector[index + 1] * 0.22]);
    }
    return points;
}

function drawPhotograph(vectors) {
    photographContext.clearRect(0, 0, photographCanvas.width, photographCanvas.height);

    for (const vector of vectors) {
        const points = getPhotographPoints(vector);
        drawHandPoints(
            photographContext,
            photographCanvas.width,
            photographCanvas.height,
            points,
            'blue',
            'green'
        );
    }

    photographStatus.textContent = vectors.length
        ? `Photographed ${vectors.length} hand vector(s)`
        : 'No hand was visible';
}

function loadSavedGestures() {
    const saved = localStorage.getItem(storageKey);
    return saved ? JSON.parse(saved) : [];
}

function showSavedGestures() {
    savedGesturesElement.replaceChildren();

    for (const gesture of loadSavedGestures()) {
        const item = document.createElement('p');
        item.textContent = gesture.name;
        savedGesturesElement.appendChild(item);
    }
}

function saveGesture() {
    const name = gestureName.value.trim();
    if (!name) {
        saveGestureStatus.textContent = 'Enter a gesture name';
        return;
    }
    if (!photographedVectors.length) {
        saveGestureStatus.textContent = 'Photograph a gesture first';
        return;
    }

    const gestures = loadSavedGestures();
    gestures.push({ name, vectors: photographedVectors });
    localStorage.setItem(storageKey, JSON.stringify(gestures));
    saveGestureStatus.textContent = `Saved ${name}`;
    gestureName.value = '';
    showSavedGestures();
}

async function photographLandmarks() {
    photographStatus.textContent = 'Photographing...';
    try {
        const response = await fetch('/tracking.json', { cache: 'no-store' });
        if (!response.ok) throw new Error(`tracking request failed: ${response.status}`);
        const photographedTracking = await response.json();
        photographedVectors = (photographedTracking.hands || [])
            .filter(hand => hand.landmarks?.length === 21)
            .map(hand => normalizeHand(hand.landmarks))
            .filter(vector => vector.every(Number.isFinite));
        drawPhotograph(photographedVectors);
    } catch {
        photographStatus.textContent = 'Photograph failed';
    }
}

photographButton.addEventListener('click', photographLandmarks);
saveGestureButton.addEventListener('click', saveGesture);
showSavedGestures();
