// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import * as vm from 'vm';
import { Kind, Scanner } from '../mediaquery/scanner';
import { isPrimitive } from './checks';

/**
 * Creates a reusable safe-eval sandbox to execute code in.
 */
export function createSandbox(): <T>(code: string, context?: any) => T {
  const sandbox = vm.createContext({});
  return (code: string, context?: any) => {
    const response = 'SAFE_EVAL_' + Math.floor(Math.random() * 1000000);
    sandbox[response] = {};
    if (context) {
      Object.keys(context).forEach(key => sandbox[key] = context[key]);
      vm.runInContext(`try {  ${response} = ${code} } catch (e) { ${response} = undefined }`, sandbox);
      for (const key of Object.keys(context)) {
        delete sandbox[key];
      }
    } else {
      vm.runInContext(`${response} = ${code}`, sandbox);
    }
    return sandbox[response];
  };
}

export const safeEval = createSandbox();

function isValue(cursor: Scanner) {
  switch (cursor.kind) {
    case Kind.NumericLiteral:
    case Kind.StringLiteral:
    case Kind.BooleanLiteral:
      cursor.take();
      return true;
  }
  cursor.takeWhitespace();

  while (cursor.kind === Kind.Identifier) {
    cursor.take();
    cursor.takeWhitespace();
    if (cursor.eof) {
      // at the end of an identifier, so it's good
      return true;
    }

    // otherwise, it had better be a dot
    if (<any>cursor.kind !== Kind.Dot) {
      // the end of the value
      return true;
    }

    // it's a dot, so take it and keep going
    cursor.take();
  }

  // if it's not an identifier, not it's valid
  return false;
}

const comparisons = [Kind.EqualsEquals, Kind.ExclamationEquals, Kind.LessThan, Kind.LessThanEquals, Kind.GreaterThan, Kind.GreaterThanEquals, Kind.EqualsEqualsEquals, Kind.ExclamationEqualsEquals];

export function valiadateExpression(expression: string) {
  // supports <value> <comparison> <value>
  if (!expression) {
    return true;
  }

  const cursor = new Scanner(expression);

  cursor.scan();
  cursor.takeWhitespace();
  if (cursor.eof) {
    // the expression is empty
    return true;
  }
  if (!isValue(cursor)) {
    return false;
  }
  cursor.takeWhitespace();
  if (cursor.eof) {
    // techincally just a value, so it's valid
    return true;
  }

  if (!(comparisons.indexOf(cursor.kind) !== -1)) {
    // can only be a comparitor at this point
    return false;
  }

  cursor.take();
  cursor.takeWhitespace();

  if (!(isValue(cursor))) {
    // can only be a value at this point
    return false;
  }

  cursor.take();
  cursor.takeWhitespace();
  if (!cursor.eof) {
    return false;
  }
  return true;
}

export function proxifyObject(obj: Record<string, any>): any {
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
