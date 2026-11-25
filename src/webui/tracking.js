const handConnections = [
    [0, 1], [1, 2], [2, 3], [3, 4],
    [0, 5], [5, 6], [6, 7], [7, 8],
    [5, 9], [9, 10], [10, 11], [11, 12],
    [9, 13], [13, 14], [14, 15], [15, 16],
    [13, 17], [17, 18], [18, 19], [19, 20],
    [0, 17]
];

function drawHandPoints(drawingContext, width, height, points, lineColour, pointColour) {
    drawingContext.strokeStyle = lineColour;
    drawingContext.lineWidth = Math.max(2, width / 350);

    for (const [start, end] of handConnections) {
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
