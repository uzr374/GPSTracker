const channelId = "";
const apiKey = "";
const maxDataPointsPerRequest = 8000;
const delayBetweenRequests = 2000;
let gps_datapoints = [];
const maxEntriesPerRequest = 500;
let polylineGroup = null;

async function loadDataPoints(lastEntryId = 0) {
try {
const response = await fetch(`https://api.thingspeak.com/channels/${channelId}/feeds.json?api_key=${apiKey}&results=1`);
if (!response.ok) {
throw new Error(`Failed to fetch data. Status: ${response.status}`);
}
const data = await response.json();
const totalEntries = data.channel.last_entry_id;
if (totalEntries == lastEntryId) {
return null;
}
const numPages = (lastEntryId == 0) ? Math.ceil(totalEntries / maxEntriesPerRequest) : Math.ceil((totalEntries - lastEntryId) / maxEntriesPerRequest);
let dataPoints = [];
for (let i = 0; i < numPages; i++) {
const offset = i * maxEntriesPerRequest;
const response = await fetch(`https://api.thingspeak.com/channels/${channelId}/feeds.json?api_key=${apiKey}&results=${maxEntriesPerRequest}&offset=${lastEntryId + offset}`);
if (!response.ok) {
throw new Error(`Failed to fetch data. Status: ${response.status}`);
}
const feeds = (await response.json()).feeds;
for (const feed of feeds) {
const {
field1,
field2,
field3,
field4,
field5,
created_at,
entry_id
} = feed;
if (entry_id > lastEntryId) {
const dataPoint = {
latitude: parseFloat(field1),
longitude: parseFloat(field2),
altitude: parseFloat(field3),
speed: parseFloat(field4),
satellites: parseInt(field5),
created_at: created_at,
entry_id: parseInt(entry_id)
};
dataPoints.push(dataPoint);
}
}
}
return dataPoints;
} catch (error) {
console.error(error);
return null;
}
}
// Helper function to get a color for polyline segment based on speed
function getSpeedBasedColor(speed) {
const colorScale = chroma.scale(["blue", "red"]).domain([0, 20]);
return colorScale(speed).hex();
}
// Helper function to determine if there should be a break in the polyline
function isBreakCondition(dpoint1, dpoint2) {
const timeThreshold = 30.0; // 30 seconds between updates
const distThreshold = 2000.0; // 2 km distance between points
const date1 = new Date(dpoint1.created_at);
const date2 = new Date(dpoint2.created_at);
const timeDiff = Math.abs(date2.getTime() - date1.getTime()) / 1000.0;
const point1 = L.latLng(dpoint1.latitude, dpoint1.longitude);
const point2 = L.latLng(dpoint2.latitude, dpoint2.longitude);
const distKm = point1.distanceTo(point2) / 1000.0;
return timeDiff > timeThreshold || distKm > distThreshold;
}
var map = null;
var cur_pos_marker = null,
start_pos_marker = null;
var initial_zoom = false;
function maybeCenterOnCurrentPos() {
if (gps_datapoints.length < 2) {
return false;
}
const cur_pos = gps_datapoints[gps_datapoints.length - 1];
map.setView([cur_pos.latitude, cur_pos.longitude], 18);
return true;
}
function doInitialCenterOnCurrentPos() {
if (!initial_zoom) {
initial_zoom = maybeCenterOnCurrentPos();
}
}

function centerOnSource() {
if (gps_datapoints.length > 1) {
const source_pos = gps_datapoints[0];
map.setView([source_pos.latitude, source_pos.longitude], 18);
}
}
function setupMap() {
map = L.map('mapid').setView([0.0, 0.0], 1);
// Add OpenStreetMap layer
var osmLayer = L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
});
// Add Satellite layer
var satelliteLayer = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', {
attribution: 'Tiles &copy; Esri &mdash; Source: Esri, i-cubed, USDA, USGS, AEX, GeoEye, Getmapping, Aerogrid, IGN, IGP, UPR-EGP, and the GIS User Community',
maxZoom: 18 //Satellite maps from this server are only available in this resolution
});
osmLayer.addTo(map);
polylineGroup = L.layerGroup();
polylineGroup.addTo(map);
// Create a Layer Control object and add it to the map
var baseMaps = {
"Street view(OpenStreetMap)": osmLayer,
"Satellite view(ArcGIS)": satelliteLayer
};
L.control.layers(baseMaps).addTo(map);
start_pos_marker = L.marker([0.0, 0.0], {
opacity: 0
}).addTo(map);
cur_pos_marker = L.marker([0.0, 0.0], {
opacity: 0
}).addTo(map);
}

function updateMarkersPos() {
if (gps_datapoints.length > 1) {
const cur_point = gps_datapoints[gps_datapoints.length - 1];

$('#gps-coordinates').text(`${cur_point.latitude.toFixed(6)}, ${cur_point.longitude.toFixed(6)}`);
$('#altitude').text(`${cur_point.altitude.toFixed(1)} m`);
$('#speed').text(`${cur_point.speed.toFixed(1)} km/h`);
$('#satellite-count').text(cur_point.satellites);
$('#last-update').text(new Date(cur_point.created_at).toLocaleString());

 // Calculate time difference since the last update
 const currentTime = new Date().getTime();
 const lastUpdateTime = new Date(cur_point.created_at).getTime();
 const timeDiffSeconds = (currentTime - lastUpdateTime) / 1000;

 // Update device status based on the time difference
 if (timeDiffSeconds <= 30) {
   $('#device-status').text("Online");
 } else {
   $('#device-status').text("Offline");
 }

const start_point = gps_datapoints[0];
cur_pos_marker.setLatLng([cur_point.latitude, cur_point.longitude]);
start_pos_marker.setLatLng([start_point.latitude, start_point.longitude]);
cur_pos_marker.setOpacity(1);
start_pos_marker.setOpacity(1);
doInitialCenterOnCurrentPos();

if (polylineGroup) {
  polylineGroup.clearLayers();
}


if ($('#display-path').is(':checked')) {
  for (let i = 1; i < gps_datapoints.length; ++i) {
    const dpoint1 = gps_datapoints[i - 1];
    const dpoint2 = gps_datapoints[i];
    if (!isBreakCondition(dpoint1, dpoint2)) {
      const polylineStyle = {
        color: getSpeedBasedColor(dpoint1.speed),
        weight: 6,
        opacity: 0.5,
        dashArray: '5,5',
      };
      const polyline = L.polyline(
        [
          [dpoint1.latitude, dpoint1.longitude],
          [dpoint2.latitude, dpoint2.longitude],
        ],
        polylineStyle
      );
      polyline.addTo(polylineGroup);
    }
  }
}
}
}

function loadNewData() {
const cur_entry_id = (gps_datapoints.length == 0) ? 0 : gps_datapoints[gps_datapoints.length - 1].entry_id;
loadDataPoints(cur_entry_id).then((dataPoints) => {
if (dataPoints) {
const new_points = dataPoints;
gps_datapoints = gps_datapoints.concat(new_points);
} 
updateMarkersPos();
}).catch((error) => {
console.error("Error occurred while loading data:", error);
});
}

function setupGUIControls() {
// Attach event handler to checkbox
$('#display-path').change(function() {
  updateMarkersPos(); // Update the polyline based on the checkbox state
});

// Attach event handlers to buttons
$('#focus-source').click(centerOnSource);
$('#focus-current-location').click(maybeCenterOnCurrentPos);
$('#force-update').click(loadNewData);
}

function initializeApp() {
setupMap();
loadNewData();
setInterval(loadNewData, 5000);
setupGUIControls();
}
$(document).ready(initializeApp);