import base64, html
C='/tmp/claude-0/ai/clips/'
LABEL={'clean':'Studio (no noise)','in':'Before','classic':'Voxology now','ai':'AI'}
CASES=[
 ('A','Female singer + home noise','Listen to the held notes: the AI takes chunks out of them.','AI removes more noise, but cuts into the singing (44 % of the sung moments turned down by over 6 dB). Voxology now: less noise removed, singing intact.'),
 ('B','Female rapper + home noise','Rap is close to speech, which the AI was trained on.','The AI wins here: cleaner than the input and than Voxology now, with the voice intact.'),
 ('C','Male singer + bedroom echo + noise','The hardest case: echo and noise together.','The AI cuts the voice (a third of the sung moments) and dulls the top. Voxology now is gentler but leaves most of the echo.'),
 ('D','Your take: Don Birthday (your home recording)','Your takes are already fairly clean.','Both barely change it: Voxology now lowers the background a little; the AI leaves it almost as is.'),
]
def b64(p): return base64.b64encode(open(p,'rb').read()).decode()
cards=[]
for k,title,listen,verdict in CASES:
    vers=['clean','in','classic','ai'] if k!='D' else ['in','classic','ai']
    btns=''.join(f'<button class="v" data-v="{v}">{LABEL[v] if not (k=="D" and v=="in") else "Your take"}</button>' for v in vers)
    auds=''.join(f'<audio data-v="{v}" preload="auto" src="data:audio/mpeg;base64,{b64(C+f"{k}_{v}.mp3")}"></audio>' for v in vers)
    cards.append(f'''<section class="card" data-case="{k}"><h2>{html.escape(title)}</h2><p class="hint">{html.escape(listen)}</p>
<div class="row"><button class="play" aria-label="Play">&#9654;</button><div class="vs">{btns}</div></div>{auds}
<details><summary>What I measured</summary><p>{html.escape(verdict)}</p></details></section>''')
page=f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AI Cleanup Test</title><style>
:root{{--bg:#f6f4ef;--card:#fff;--ink:#1d1b18;--mute:#6b665d;--line:#e3ded3;--acc:#b8860b;--on:#1d1b18;--onink:#fff}}
@media (prefers-color-scheme:dark){{:root:not([data-theme="light"]){{--bg:#141311;--card:#1e1c19;--ink:#efebe3;--mute:#a39d92;--line:#34302a;--acc:#e0b04a;--on:#efebe3;--onink:#141311}}}}
:root[data-theme="dark"]{{--bg:#141311;--card:#1e1c19;--ink:#efebe3;--mute:#a39d92;--line:#34302a;--acc:#e0b04a;--on:#efebe3;--onink:#141311}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}}
main{{max-width:720px;margin:0 auto;padding:20px 16px 48px}}h1{{font-size:1.4rem;margin:0 0 6px}}.lead{{color:var(--mute);margin:0 0 18px}}
.sum{{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px;margin-bottom:18px}}.sum li{{margin:4px 0}}
.card{{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px;margin-bottom:14px}}
h2{{font-size:1.05rem;margin:0 0 4px}}.hint{{color:var(--mute);margin:0 0 10px;font-size:.95rem}}
.row{{display:flex;gap:10px;align-items:center}}.vs{{display:flex;flex-wrap:wrap;gap:6px}}
button{{font:inherit;cursor:pointer;border:1px solid var(--line);background:transparent;color:var(--ink);border-radius:999px;padding:7px 12px}}
button.v.on{{background:var(--on);color:var(--onink);border-color:var(--on)}}
.play{{width:44px;height:44px;border-radius:50%;flex:none;border-color:var(--acc);color:var(--acc);font-size:1rem;padding:0}}
details{{margin-top:10px;color:var(--mute);font-size:.95rem}}summary{{cursor:pointer}}
</style></head><body><main>
<h1>AI cleanup: step 1 test</h1>
<p class="lead">Each clip plays from the same spot when you switch, all at the same loudness. Headphones help.</p>
<div class="sum"><strong>In short</strong><ul>
<li>The best open AI noise remover we're allowed to sell with (DeepFilterNet3) works on speech and speech-like rap.</li>
<li>On singing it cuts into held notes. As it is, it's not good enough for Voxology.</li>
<li>Its cost is fine: about 3 % of one computer core and about 30 ms of delay.</li>
<li>Neither it nor Voxology's current Room really removes a bedroom's echo.</li></ul></div>
{''.join(cards)}
</main><script>
document.querySelectorAll('.card').forEach(card=>{{
  const auds=[...card.querySelectorAll('audio')], btns=[...card.querySelectorAll('button.v')], play=card.querySelector('.play');
  let cur=auds.find(a=>a.dataset.v==='in')||auds[0]; btns.forEach(x=>x.classList.toggle('on',x.dataset.v===cur.dataset.v));
  const stopOthers=()=>document.querySelectorAll('audio').forEach(a=>{{if(!auds.includes(a)){{a.pause()}}}});
  btns.forEach(b=>b.onclick=()=>{{const n=auds.find(a=>a.dataset.v===b.dataset.v);const t=cur.currentTime,wasPlaying=!cur.paused;cur.pause();n.currentTime=t;cur=n;
    btns.forEach(x=>x.classList.toggle('on',x===b));if(wasPlaying){{n.play()}}}});
  play.onclick=()=>{{if(cur.paused){{stopOthers();document.querySelectorAll('.play').forEach(p=>p.innerHTML='&#9654;');cur.play();play.innerHTML='&#10074;&#10074;'}}else{{cur.pause();play.innerHTML='&#9654;'}}}};
  auds.forEach(a=>a.onended=()=>{{play.innerHTML='&#9654;'}});
}});
</script></body></html>'''
open('/tmp/claude-0/-home-user/9fe6932e-7555-5b2b-b3a9-25b1247764b6/scratchpad/ai-cleanup-test.html','w').write(page)
print(len(page))
