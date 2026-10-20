// ?t=light picks the light screenshots and colours; ?f=square or ?f=story the shape
// (1080 x 1080, 1080 x 1920; the post shape, 1080 x 1350, otherwise)
const params = new URLSearchParams(location.search);
const t = params.get("t") || "dark";
document.documentElement.dataset.theme = t;
document.documentElement.dataset.format = params.get("f") || "post";
for (const img of document.querySelectorAll("img[data-shot]")) img.src = `shots/${t}/${img.dataset.shot}`;
