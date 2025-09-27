#!/usr/bin/env node
/**
 * build-sliders.js
 * Usage:
 *   node build-sliders.js --xml "/path/to/BHUNP3BBB.xml" --osp "/path/to/BHUNP 3BBB Advanced.osp" --out "/path/to/output.txt"
 *
 * Reads:
 *   - XML: categories and sliders (names + optional displayname)
 *   - OSP: slider defaults (small, big) and invert flag
 *
 * Writes a text file structured as:
 * {{GROUP}}
 * Slider name: {{sliderName}}; Display name: {{displayName}}; Small default value: {{smallDefaultValue}}; BigDefaultValue: {{bigDefaultValue}}; inverted: {{isInverted}}
 */
const fs = require('fs');
const path = require('path');
const { XMLParser } = require('fast-xml-parser');

function parseArgs(argv) {
  const args = {};
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--xml') args.xml = argv[++i];
    else if (a === '--osp') args.osp = argv[++i];
    else if (a === '--out') args.out = argv[++i];
    else if (a === '--help' || a === '-h') {
      console.log('Usage: node build-sliders.js --xml <file.xml> --osp <file.osp> --out <output.txt>');
      process.exit(0);
    }
  }
  if (!args.xml || !args.osp || !args.out) {
    console.error('Missing required arguments. Run with --help for usage.');
    process.exit(1);
  }
  return args;
}

function loadXml(filePath) {
  const xml = fs.readFileSync(filePath, 'utf8');
  const parser = new XMLParser({
    ignoreAttributes: false,
    attributeNamePrefix: '',
    allowBooleanAttributes: true,
    // Keep order simple; we just need attributes and element arrays
  });
  return parser.parse(xml);
}

function toArray(x) {
  if (x === undefined || x === null) return [];
  return Array.isArray(x) ? x : [x];
}

function buildOspMap(ospObj) {
  // Expected path: SliderSetInfo -> SliderSet -> Slider (array)
  const sets = toArray(ospObj?.SliderSetInfo?.SliderSet);
  const map = new Map();
  for (const s of sets) {
    for (const sl of toArray(s?.Slider)) {
      const name = sl?.name ?? sl?.['@_name'];
      // fast-xml-parser with ignoreAttributes:false uses plain keys
      const small = sl?.small ?? sl?.['@_small'];
      const big = sl?.big ?? sl?.['@_big'];
      const invert = sl?.invert ?? sl?.['@_invert'];
      if (name) {
        map.set(String(name), {
          smallDefaultValue: small !== undefined ? String(small) : '0',
          bigDefaultValue: big !== undefined ? String(big) : '0',
          isInverted: invert !== undefined ? String(invert) : 'false',
        });
      }
    }
  }
  return map;
}

function generateOutput(xmlObj, ospMap) {
  // XML expected path: SliderCategories -> Category[] -> name attr, contains Slider[] with name/displayname
  const categories = toArray(xmlObj?.SliderCategories?.Category);
  const lines = [];
  for (const cat of categories) {
    const groupName = cat?.name ?? cat?.['@_name'] ?? 'Unknown';
    lines.push(`${groupName.toUpperCase()}`);
    const sliders = toArray(cat?.Slider);
    for (const slider of sliders) {
      const sliderName = slider?.name ?? slider?.['@_name'] ?? 'Unknown';
      const displayName = slider?.displayname ?? slider?.displayName ?? slider?.['@_displayname'] ?? slider?.['@_displayName'] ?? sliderName;
      const defs = ospMap.get(String(sliderName)) || {
        smallDefaultValue: '0',
        bigDefaultValue: '0',
        isInverted: 'false',
      };
      lines.push(
        `Slider name: ${sliderName}; ` +
        `Display name: ${displayName}; ` +
        `Small default value: ${defs.smallDefaultValue}; ` +
        `BigDefaultValue: ${defs.bigDefaultValue}; ` +
        `inverted: ${defs.isInverted}`
      );
    }
    lines.push(''); // blank line between groups
  }
  return lines.join('\n');
}

(async function main() {
  const { xml, osp, out } = parseArgs(process.argv);
  try {
    const xmlObj = loadXml(xml);
    const ospObj = loadXml(osp);
    const ospMap = buildOspMap(ospObj);
    const output = generateOutput(xmlObj, ospMap);
    fs.writeFileSync(out, output, 'utf8');
    console.log(`Wrote ${out} (${output.split('\\n').length} lines)`);
  } catch (err) {
    console.error('Error:', err.message);
    process.exit(1);
  }
})();