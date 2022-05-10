// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Document, Node, Pair, parseDocument, Scalar, stringify, visit, YAMLMap, YAMLSeq } from 'yaml';
import { StringOrStrings } from '../interfaces/metadata/metadata-format';

/** @internal */
export const createNode = (v: any, b = true) => parseDocument('', { prettyErrors: false }).createNode(v, {});

/** @internal */
export function getOrCreateMap(node: Document.Parsed | YAMLMap, name: string): YAMLMap {
  let m = node.get(name);
  if (m) {
    return <any>m;
  }

  node.set(name, m = new YAMLMap());
  return <any>m;
}

export function getStrings(node: Document.Parsed | YAMLMap, name: string): Array<string> {
  const r = node.get(name);
  if (r) {
    if (r instanceof YAMLSeq) {
      return r.items.map((each: any) => each.value);
    }
    if (typeof r === 'string') {
      return [r];
    }
  }
  return [];
}

export function setStrings(node: Document.Parsed | YAMLMap, name: string, value: StringOrStrings) {
  if (Array.isArray(value)) {
    switch (value.length) {
      case 0:
        return node.set(name, undefined);
      case 1:
        return node.set(name, value[0]);
    }
    return node.set(name, createNode(value, true));
  }
  node.set(name, value);
}

export function getPair(from: YAMLMap, name: string): Pair<Node, string> | undefined {
  return <any>from.items.find((each: any) => (<Scalar>each.key).value === name);
}

export function serialize(value: any) {
  const document = new Document(value);
  visit(document, {
    Seq: (k, n, p) => {
      // set arrays to [ ... ] instead of one value per line.
      n.flow = true;
    }
  });
  return document.toString();
}

export function isYAML(path: string) {
  path = path.toLowerCase();
  return path.endsWith('.yml') || path.endsWith('.yaml') || path.endsWith('.json');
}

export function toYAML(content: string) {
  if (content.charAt(0) === '{') {
    // the content is in JSON format.
    // let's force it to YAML
    try {
      return stringify(JSON.parse(content)).toString();
    } catch (e: any) {
      // no worries.
    }
  }
  return content;
}
