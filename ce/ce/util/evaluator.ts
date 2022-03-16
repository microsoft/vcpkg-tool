// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { lstatSync } from 'fs';
import { delimiter, resolve } from 'path';
import { safeEval } from '../util/safeEval';
import { isPrimitive } from './checks';
import path = require('path');

function proxifyObject(obj: Record<string, any>): any {
  return new Proxy(obj, {
    get(target, prop) {

      if (typeof prop === 'string') {
        let result = target[prop];
        // check for a direct match first
        if (!result) {
          // go thru the properties and check for a case-insensitive match
          for (const each of Object.keys(target)) {
            if (each.toLowerCase() === prop.toLowerCase()) {
              result = target[each];
              break;
            }
          }
        }
        if (result) {
          if (Array.isArray(result)) {
            return result;
          }
          if (typeof result === 'object') {
            return proxifyObject(result);
          }
          if (isPrimitive(result)) {
            return result;
          }
        }
        return undefined;
      }
    },
  });

}

export class Evaluator {
  private activation: any;
  private host: any;

  constructor(private artifactData: Record<string, string>, host: Record<string, any>, activation: Record<string, any>) {
    this.host = proxifyObject(host);
    this.activation = proxifyObject(activation);
  }

  private resolve(expression: string) {
    const quick = `$${expression}`;
    if (this.artifactData[quick] !== undefined) {
      return this.artifactData[quick];
    }

    if (expression.startsWith('host.')) {
      // this is getting something from the host context (ie, environment variable)
      return safeEval(expression.substr(5), this.host) || undefined;
    }

    // otherwise, assume it is a property on the activation object
    return safeEval(expression, this.activation) || undefined;
  }

  private replaceVariables(text: string): string {
    if (text.indexOf('$') === -1) {
      // quick exit if no expression or no variables
      return text;
    }

    // $$ -> escape for $
    text = text.replace(/\$\$/g, '\uffff');

    // $0 ... $9 -> replace contents with the values from the artifact
    text = text.replace(/\$([0-9])/g, (match, index) => this.artifactData[match] || match);

    // $<expression> -> expression value
    text = text.replace(/\$([a-zA-Z_.][a-zA-Z0-9_.]*)/g, (match, expression) => this.resolve(expression) || match);

    // restore escaped $
    text = text.replace(/\uffff/g, '$');

    return text;
  }

  evaluate(text: string | undefined): string | undefined {
    // short circuit if nothing to evaluate
    if (!text || typeof text !== 'string') {
      return text;
    }

    // handle variable substitution
    if (text.indexOf('$') !== -1) {
      text = this.replaceVariables(text);
    }

    // { ...} verifies that the expression is a valid existing path on disk.
    text = text.replace(/\{(.*?)\}/g, (match, expression) => {
      try {
        const path = resolve(expression);
        lstatSync(path);
        return path;
      } catch {
        // shh
      }

      return match;
    });

    return text;
  }

  expandPathLikeVariableExpressions(value: string, delim = delimiter): Array<string> {
    let n = undefined;
    const parts = value.split(/(\$[a-zA-Z0-9.]+)/g).filter(each => each).map((part, i) => {

      const value = this.evaluate(part) || '';
      if (value.indexOf(delim) !== -1) {
        n = i;
      }

      return value;
    });

    if (n === undefined) {
      // if the value didn't have a path separator, then just return the value
      return [parts.join('')];
    }

    const front = parts.slice(0, n).join('');
    const back = parts.slice(n + 1).join('');

    return parts[n].split(delim).filter(each => each).map(each => `${front}${each}${back}`);
  }
}