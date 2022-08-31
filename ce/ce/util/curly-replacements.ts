// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from "../i18n";

export function replaceCurlyBraces(subject: string, properties: Map<string, string>) {
  // One of these tokens:
  // {{
  // }}
  // {variable}
  // {
  // }
  // (anything that has no {}s)
  const tokenRegex = /{{|}}|{([^}]+)}|{|}|[^{}]+/y;
  const resultElements : Array<string> = [];
  for (;;) {
    const thisMatch = tokenRegex.exec(subject);
    if (thisMatch === null) {
      return resultElements.join('');
    }

    const wholeMatch = thisMatch[0];
    if (wholeMatch === '{{') {
      resultElements.push('{');
      continue;
    }

    if (wholeMatch === '}}') {
      resultElements.push('}');
      continue;
    }

    if (wholeMatch === '{' || wholeMatch === '}') {
      throw new Error(i`Found a mismatched ${wholeMatch} in '${subject}'. For a literal ${wholeMatch}, use ${wholeMatch}${wholeMatch} instead.`);
    }

    const variableName = thisMatch[1];
    if (variableName) {
      const variableValue = properties.get(variableName);
      if (typeof variableValue !== 'string') {
        throw new Error(i`Could not find a value for {${variableName}} in '${subject}'. To write the literal value, use '{{${variableName}}}' instead.`);
      }

      resultElements.push(variableValue);
      continue;
    }

    resultElements.push(wholeMatch);
  }
}
