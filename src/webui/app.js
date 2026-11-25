const image = document.querySelector('#frame');
const canvas = document.querySelector('#overlay');
const context = canvas.getContext('2d');
const photographButton = document.querySelector('#photograph-button');
const photographCanvas = document.querySelector('#photograph');
const photographContext = photographCanvas.getContext('2d');
const photographStatus = document.querySelector('#photograph-status');
const confidenceInputs = {
    palm: document.querySelector('#palm-confidence'),
    landmark: document.querySelector('#landmark-confidence')
};
let confidenceTimer;

// load current thresholds, display changes, send slider updates
async function loadConfidence() {
    const response = await fetch('/confidence');
    const values = await response.json();
    for (const [name, input] of Object.entries(confidenceInputs)) {
        input.value = values[name];
        document.querySelector(`#${name}-value`).value = Number(values[name]).toFixed(2);
        input.addEventListener('input', () => {
            document.querySelector(`#${name}-value`).value = Number(input.value).toFixed(2);
            // debounce requests, avoid posting every slider event
            clearTimeout(confidenceTimer);
            confidenceTimer = setTimeout(() => {
                const query = new URLSearchParams({
                    palm: confidenceInputs.palm.value,
                    landmark: confidenceInputs.landmark.value
                });
                fetch(`/confidence?${query}`, { method: 'POST' });
            }, 100);
        });
    }
}

// landmark index pairs, finger bones and palm links
const connections = [
    [0, 1],[1, 2],[2, 3],[3, 4],
    [0, 5],[5, 6],[6, 7],[7, 8],
    [5, 9],[9, 10],[10, 11],[11, 12],
    [9, 13],[13, 14],[14, 15],[15, 16],
    [13, 17],[17, 18],[18, 19],[19, 20],
    [0,17]
];

let tracking = { palms: [], hands: [] };
let photographedVectors = [];

function drawHandPoints(drawingContext, width, height, points, lineColour, pointColour) {
    drawingContext.strokeStyle = lineColour;
    drawingContext.lineWidth = Math.max(2, width / 350);
    for (const [start, end] of connections) {
        drawingContext.beginPath();
        drawingContext.moveTo(points[start][0] * width, points[start][1] * height);
        drawingContext.lineTo(points[end][0] * width, points[end][1] * height);
        drawingContext.stroke();
    }

    drawingContext.fillStyle = pointColour;
    for (const [x, y] of points) {
        drawingContext.beginPath();
        drawingContext.arc(x * width, y * height, Math.max(3, width / 250), 0, 2 * Math.PI);
        drawingContext.fill();
    }
}

function normalizeHand(landmarks) {
    const wrist = landmarks[0];
    const middleFinger = landmarks[9];
    const palmLandmarks = [landmarks[0], landmarks[5], landmarks[9], landmarks[13], landmarks[17]];
    const palmX = palmLandmarks.reduce((sum, point) => sum + point[0], 0) / 5;
    const palmY = palmLandmarks.reduce((sum, point) => sum + point[1], 0) / 5;
    const palmSize = Math.hypot(middleFinger[0] - wrist[0], middleFinger[1] - wrist[1]);

    return landmarks.flatMap(point => [
        (point[0] - palmX) / palmSize,
        (point[1] - palmY) / palmSize
    ]);
}

async function photographLandmarks() {
    // console.log('[gestured] photograph clicked');
    photographStatus.textContent = 'Photographing...';
    try {
        const response = await fetch('/tracking.json', { cache: 'no-store' });
        if (!response.ok) throw new Error(`tracking request failed: ${response.status}`);
        const photographedTracking = await response.json();
        photographedVectors = (photographedTracking.hands || [])
            .filter(hand => hand.landmarks?.length === 21)
            .map(hand => normalizeHand(hand.landmarks))
            .filter(vector => vector.every(Number.isFinite));
        // console.log('[gestured] photograph received hands', photographedVectors.length);
        drawPhotograph();
    } catch (error) {
        // console.error('[gestured] photograph failed', error);
        photographStatus.textContent = 'Photograph failed';
    }
}

function drawPhotograph() {
    photographContext.clearRect(0, 0, photographCanvas.width, photographCanvas.height);

    for (const vector of photographedVectors) {
        const scale = photographCanvas.width * 0.22;
        const points = [];
        for (let index = 0; index < vector.length; index += 2) {
            points.push([
                0.5 + vector[index] * scale / photographCanvas.width,
                0.5 + vector[index + 1] * scale / photographCanvas.height
            ]);
        }
        drawHandPoints(photographContext, photographCanvas.width, photographCanvas.height, points, 'blue', 'green');
    }

    photographStatus.textContent = photographedVectors.length
        ? `Photographed ${photographedVectors.length} hand vector(s)`
        : 'No hand was visible';
}

// draw a closed outline from frame relative points
function polygon(points, colour) {
    if (!points || points.length < 2) return;

    context.strokeStyle = colour;
    context.lineWidth = Math.max(2, canvas.width / 500);
    context.beginPath();
    // scale normalised coordinates to the current image size
    context.moveTo(points[0][0] * canvas.width, points[0][1] * canvas.height);
    for (const [x,y] of points.slice(1)) {
        context.lineTo(x * canvas.width, y * canvas.height);
    }
    context.closePath();
    context.stroke();
}

// redraw palm boxes, hand connections, landmarks over the current frame
function draw() {
    // wait for image dimensions before drawing the overlay
    if (!canvas.width || !canvas.height) return;

    context.clearRect(0, 0, canvas.width, canvas.height);

    // show the original palm box, then its rotation adjusted crop
    for (const palm of tracking.palms) {
        polygon(palm.box, 'orange');
        polygon(palm.rotated_box, 'red');
    }

    // connect the 21 landmarks, draw each point over the image
    for (const hand of tracking.hands) {
        const points = hand.landmarks;
        // skip incomplete landmark results
        if (!points || points.length !== 21) continue;
        drawHandPoints(context, canvas.width, canvas.height, points, 'blue', 'green');
    }
}

// fetch the latest tracking snapshot, refresh the overlay
async function updateTracking() {
    try {
        // avoid cached responses, tracking should reflect the latest frame
        const response = await fetch("/tracking.json", { cache: "no-store" });
        if (response.ok) tracking = await response.json();
    } catch {
        // clear stale overlays if the server cannot be reached
        tracking = { palms: [], hands: [] };
    }
    draw();
    // poll again, keep tracking updates independent from image refresh
    setTimeout(updateTracking, 100);
}

/*
// refresh the camera image, align the overlay canvas
function updateFrame() {
    image.onload = () => {
        // match overlay coordinates to the decoded camera frame
        canvas.width = image.naturalWidth;
        canvas.height = image.naturalHeight;
        draw();
        // request the next frame after the current one has loaded
        setTimeout(updateFrame, 100);
    };
    // retry more slowly when no frame is available
    image.onerror = () => setTimeout(updateFrame, 500);
    // query parameter avoids reusing a cached jpeg
    image.src = "/stream.mjpg?t=" + Date.now();
}
*/

image.addEventListener("load", () => {
    // match overlay coordinates to the decoded camera frame
    canvas.width = image.naturalWidth;
    canvas.height = image.naturalHeight;
    draw();
});
photographButton.addEventListener('click', photographLandmarks);
// start camera stream, refresh tracking and confidence controls
image.src = "/stream.mjpg";

//updateFrame();
updateTracking();
loadConfidence();