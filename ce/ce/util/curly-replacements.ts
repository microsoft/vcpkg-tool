// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

export function replaceCurlyBraces(subject: string, properties: Map<string, string>) {
  // One of these tokens:
  // {{
  // }}
  // {variable}
  // {
  // }
  // (anything that has no {s)
  const tokenRegex = /{{|}}|{([^}]+)}|{|}|[^{}]+/y;
  const resultElements : Array<string> = [];
  for (;;) {
    const thisMatch = tokenRegex.exec(subject);
    if (thisMatch === null) {
      return resultElements.join('');
    }

    const wholeMatch = thisMatch[0];
    if (wholeMatch === '{{' || wholeMatch === '{') {
      resultElements.push('{');
      continue;
    }

    if (wholeMatch === '}}' || wholeMatch === '}') {
      resultElements.push('}');
      continue;
    }

    const variableName = thisMatch[1];
    if (variableName) {
      const variableValue = properties.get(variableName);
      if (typeof variableValue !== 'string') {
        throw new Error('Could not find a value for {' + variableName + '} in \'' + subject + '\'. To write the literal value, use \'{{' + variableName + '}}\' instead.');
      }

      resultElements.push(variableValue);
      continue;
    }

    resultElements.push(wholeMatch);
  }
}
