// ?t=light picks the light screenshots and colours
const t = new URLSearchParams(location.search).get("t") || "dark";
document.documentElement.dataset.theme = t;
for (const img of document.querySelectorAll("img[data-shot]")) img.src = `shots/${t}/${img.dataset.shot}`;
