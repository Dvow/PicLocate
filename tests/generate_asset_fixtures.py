from pathlib import Path
from PIL import Image, ImageDraw
import json
import argparse
parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
args = parser.parse_args()
root = args.directory.resolve(); root.mkdir(parents=True, exist_ok=True)
colors={'red':(220,55,48),'blue':(40,95,230),'green':(40,190,90),'purple':(160,65,220)}
truth={}
for shape in ['sword','shield','potion','gem','ring','key']:
    for color,rgb in colors.items():
        for variant in range(4):
            im=Image.new('RGBA',(128,128),(19,77,159,0));d=ImageDraw.Draw(im)
            fill=(*rgb,255); edge=(245,235,200,255)
            if shape=='sword':
                d.polygon([(64,12),(75,31),(73,85),(55,85),(53,31)],fill=fill,outline=edge,width=3)
                d.rectangle((39,80,89,89),fill=edge);d.rectangle((59,87,69,112),fill=(95,65,35,255))
            elif shape=='shield':
                d.polygon([(26,23),(64,13),(102,23),(96,76),(64,111),(32,76)],fill=fill,outline=edge,width=4)
                d.line((64,23,64,98),fill=edge,width=4)
            elif shape=='potion':
                d.ellipse((29,42,99,112),fill=fill,outline=edge,width=3)
                d.rectangle((49,20,79,54),fill=fill,outline=edge,width=3);d.rectangle((46,14,82,24),fill=(95,65,35,255))
            elif shape=='gem':
                d.polygon([(37,22),(91,22),(110,53),(64,111),(18,53)],fill=fill,outline=edge,width=3)
                d.line((37,22,49,53,64,111,79,53,91,22),fill=edge,width=2);d.line((18,53,110,53),fill=edge,width=2)
            elif shape=='ring':
                d.ellipse((28,31,100,113),outline=fill,width=15)
                d.polygon([(48,15),(80,15),(88,35),(64,51),(40,35)],fill=fill,outline=edge,width=3)
            elif shape=='key':
                d.ellipse((28,13,82,66),outline=fill,width=12);d.rectangle((50,57,61,113),fill=fill)
                d.rectangle((57,83,82,95),fill=fill);d.rectangle((57,101,75,111),fill=fill)
            if variant==1:d.ellipse((60,50,67,57),fill=edge)
            if variant==2:
                padded=Image.new('RGBA',(192,192),(222,17,4,0));padded.paste(im,(32,32));im=padded
            if variant==3:
                im=im.resize((112,112),Image.Resampling.LANCZOS)
            # Opaque IDs prevent filenames from giving the answer to either ranker.
            name=f'{len(truth)+1:05}.png';im.save(root/name)
            truth[name]={'family':f'{color}-{shape}','variant':variant}
(root/'truth.json').write_text(json.dumps(truth,indent=2))
print(f'{len(truth)} procedural asset fixtures')
