const image = document.querySelector('#frame');
const canvas = document.querySelector('#overlay');
const context = canvas.getContext('2d');
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

        context.strokeStyle = 'blue';
        context.lineWidth = Math.max(2, canvas.width / 350);
        for (const [start, end] of connections) {
            context.beginPath();
            context.moveTo(points[start][0] * canvas.width, points[start][1] * canvas.height);
            context.lineTo(points[end][0] * canvas.width, points[end][1] * canvas.height);
            context.stroke();
        }

        context.fillStyle = 'green';
        for (const [x, y] of points) {
            context.beginPath();
            context.arc(x * canvas.width, y * canvas.height, Math.max(3, canvas.width / 250), 0, 2 * Math.PI);
            context.fill();
        }
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
// start camera stream, refresh tracking and confidence controls
image.src = "/stream.mjpg";

//updateFrame();
updateTracking();
loadConfidence();