// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { XMLBuilder, XmlBuilderOptionsOptional } from 'fast-xml-parser';

const defaultOptions = {
  attributeNamePrefix: '$',
  textNodeName: '#text',
  ignoreAttributes: false,
  ignoreNameSpace: false,
  allowBooleanAttributes: false,
  parseNodeValue: false,
  parseAttributeValue: true,
  trimValues: true,
  cdataTagName: '__cdata',
  cdataPositionChar: '\\c',
  parseTrueNumberOnly: false,
  arrayMode: false,
  format: true,
};

export function toXml(content: Record<string, any>, options: XmlBuilderOptionsOptional = {}) {
  return `<?xml version="1.0" encoding="utf-8"?>\n${new XMLBuilder({ ...defaultOptions, ...options }).build(content)}`.trim();
}

export function toXmlFragment(content: Record<string, any>, options: XmlBuilderOptionsOptional = {}) {
  return new XMLBuilder({ ...defaultOptions, ...options }).build(content);
}

