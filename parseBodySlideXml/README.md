# How to use

1) Install the single dependency(you should have nodejs installed):
```
npm i
```

2) Run the script (using your uploaded paths as an example):
```
node ./parseBodySlideXml/build-sliders.js --xml "./parseBodySlideXml/HIMBO.xml" --osp "./parseBodySlideXml/HIMBO.osp" --out "./parseBodySlideXml/slidersHIMBO.txt"
```

That will produce /mnt/data/sliders.txt with sections like:

```
{{GROUP}}
Slider name: {{sliderName}}; Display name: {{displayName}}; Small default value: {{smallDefaultValue}}; BigDefaultValue: {{bigDefaultValue}}; inverted: {{isInverted}}
```

## Notes

- The script reads groups and slider display names from your XML (e.g., SliderCategories > Category > Slider with name/displayname attributes) 
- It reads small, big, and invert defaults from your .osp file and matches by the slider’s name.
- If a slider from the XML isn’t found in the OSP, it writes 0 for the two defaults and false for inverted.
- If a slider has no displayname in the XML, it falls back to the name.