---
---

<div id="output">
    <button id="round">Run Round</button>
    <div id="graph"></div>
    <ol reversed></ol>
</div>

<script type="module">
import * as Viz from "https://unpkg.com/@viz-js/viz";

const BASE_URL = 'http://localhost:8000';

async function requestGraph() {
    const responseText = await fetch(`${BASE_URL}/graph`);
    return responseText.json();
}

var sha256;
if (typeof window === 'undefined') {
    const { createHash } = require('node:crypto');
    sha256 = async function(content) {
        return createHash('sha256').update(content).digest('hex');
    }
} else {
    sha256 = async function(message) {
        const msgBuffer = new TextEncoder().encode(message);
        const hashBuffer = await window.crypto.subtle.digest('SHA-256', msgBuffer);
        const hashArray = Array.from(new Uint8Array(hashBuffer));
        return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
    }
}

async function requestBoxedEdges() {
    const responseText = await fetch(`${BASE_URL}/boxes`);
    return responseText.json();
}

async function requestUnboxedEdge(roundId, edgeIndex) {
    const responseText = await fetch(`${BASE_URL}/edge?round_id=${roundId}&edge_index=${edgeIndex}`);
    const response = await responseText.json();
    if (typeof response !== 'object' || Array.isArray(response)) {
        throw new Error('Invalid response format');
    }
    return response;
}

async function round(edges) {
    const boxedEdges = await requestBoxedEdges();
    const hashes = boxedEdges.boxes;
    const roundId = boxedEdges.round_id;

    const randomIndex = Math.floor(Math.random() * edges.length);
    const response = await requestUnboxedEdge(roundId, randomIndex);
    const openedBoxes = response.openings;
    const edge = edges[randomIndex];
    const edge0 = `${edge[0]}`;
    const edge1 = `${edge[1]}`;
    if (!openedBoxes.hasOwnProperty(edge0) || !openedBoxes.hasOwnProperty(edge1)) {
        throw new Error('Response does not contain expected edge keys');
    }
    const {color: color0, nonce: nonce0} = openedBoxes[edge0];
    const {color: color1, nonce: nonce1} = openedBoxes[edge1];
    const hash0 = await sha256(`${color0}-${nonce0}`);
    const hash1 = await sha256(`${color1}-${nonce1}`);
    if (hash0 !== hashes[edge0] || hash1 !== hashes[edge1]) {
        throw new Error('Hash mismatch for edge colors');
    }
    if (color0 === color1) {
        throw new Error('Adjacent vertices have the same color');
    }
    const result = {};
    result[`node${edge0}`] = color0;
    result[`node${edge1}`] = color1;
    console.log(result);
    return result;
}

(async function() {
    const viz = await Viz.instance();
    const outputGraph = document.querySelector('#graph');
    const graph = await requestGraph();
    const edges = graph.edges;
    // Graphviz numbers nodes node1, node2, ... in layout order, so give each
    // one an explicit id matching its vertex name.
    const nodes = [...new Set(edges.flat())];
    const dotString = `graph {
        ${nodes.map(node => `${node} [id="node${node}"]`).join('; ')};
        ${edges.map(edge => `${edge[0]} -- ${edge[1]}`).join('; ')}
    }`;
    const svgElement = viz.renderSVGElement(dotString);
    outputGraph.appendChild(svgElement);

    const outputList = document.querySelector('#output ol');
    const roundButton = document.querySelector('#round');

    function paint(nodeId, color) {
        const node = document.querySelector(`#${nodeId}`);
        node.querySelector('ellipse').style.fill = color ?? 'none';
        node.querySelector('text').style.fill = color ? 'white' : 'black';
    }

    function logRound(msg) {
        const item = document.createElement('li');
        item.textContent = msg;
        outputList.prepend(item);
    }

    function clearColors() {
        for (const node of nodes) {
            paint(`node${node}`, null);
        }
    }

    let i = 0;
    roundButton.addEventListener('click', async () => {
        clearColors();
        try {
            const edgeColors = await round(edges);
            for (const [node, color] of Object.entries(edgeColors)) {
                paint(node, color);
            }
            const probability = 1 - Math.pow(1 - 1 / edges.length, i + 1);
            const msg = `Success. Confident ${(probability * 100).toFixed(2)}%`;
            logRound(msg);
            console.log(`Round ${i + 1}:`, msg);
        } catch (error) {
            const msg = `Failure - ${error.message}`;
            logRound(msg);
            console.error(`Round ${i + 1}:`, msg);
            throw error;
        }
        i += 1;
    });
})();
</script>
