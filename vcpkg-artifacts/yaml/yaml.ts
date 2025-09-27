// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Document, Node, Pair, parseDocument, Scalar, visit, YAMLMap, YAMLSeq } from 'yaml';

/** @internal */
export const createNode = (v: any, _b = true) => parseDocument('', { prettyErrors: false }).createNode(v, {});

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

/** values that can be either a single string, or an array of strings */
export type StringOrStrings = string | Array<string>;

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
    Seq: (_k, n, _p) => {
      // set arrays to [ ... ] instead of one value per line.
      n.flow = true;
    }
  });
  return document.toString();
}
