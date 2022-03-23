// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '../i18n';
import { Kind, MediaQueryError, Scanner, Token } from './scanner';

export function parseQuery(text: string) {
  const cursor = new Scanner(text);

  return QueryList.parse(cursor);
}

function takeWhitespace(cursor: Scanner) {
  while (!cursor.eof && isWhiteSpace(cursor)) {
    cursor.take();
  }
}

function isWhiteSpace(cursor: Scanner) {
  return cursor.kind === Kind.Whitespace;
}

class QueryList {
  queries = new Array<Query>();
  get isValid() {
    return !this.error;
  }
  error?: MediaQueryError;

  protected constructor() {
    //
  }

  get length() {
    return this.queries.length;
  }
  static parse(cursor: Scanner) {
    const result = new QueryList();

    try {
      cursor.scan(); // start the scanner
      for (const statement of QueryList.parseQuery(cursor)) {
        result.queries.push(statement);
      }
    } catch (error: any) {
      result.error = error;
    }
    return result;
  }

  static *parseQuery(cursor: Scanner): Iterable<Query> {
    takeWhitespace(cursor);
    if (cursor.eof) {
      return;
    }
    yield Query.parse(cursor);
    takeWhitespace(cursor);
    if (cursor.eof) {
      return;
    }
    switch (cursor.kind) {
      case Kind.Comma:
        cursor.take();
        return yield* QueryList.parseQuery(cursor);
      case Kind.EndOfFile:
        return;
    }
    throw new MediaQueryError(i`Expected comma, found ${JSON.stringify(cursor.text)}`, cursor.position.line, cursor.position.column);
  }

  get features() {
    const result = new Set<string>();
    for (const query of this.queries) {
      for (const expression of query.expressions) {
        if (expression.feature) {
          result.add(expression.feature);
        }
      }
    }
    return result;
  }

  match(properties: Record<string, unknown>) {
    if (this.isValid) {
      queries: for (const query of this.queries) {
        for (const { feature, constant, not } of query.expressions) {
          // get the value from the context
          const contextValue = stringValue(properties[feature]);
          if (not) {
            // negative/not present query

            if (contextValue) {
              // we have a value
              if (constant && contextValue !== constant) {
                continue; // the values are NOT a match.
              }
              if (!constant && contextValue === 'false') {
                continue;
              }
            } else {
              // no value
              if (!constant || contextValue === 'false') {
                continue;
              }
            }
          } else {
            // positive/present query
            if (contextValue) {
              if (contextValue === constant || contextValue !== 'false' && !constant) {
                continue;
              }
            } else {
              if (constant === 'false') {
                continue;
              }
            }
          }
          continue queries; // no match
        }
        // we matched a whole query, we're good
        return true;
      }
    }
    // no query matched.
    return false;
  }
}

function stringValue(value: unknown): string | undefined {
  switch (typeof value) {
    case 'string':
    case 'number':
    case 'boolean':
      return value.toString();

    case 'object':
      return value === null ? 'true' : Array.isArray(value) ? stringValue(value[0]) || 'true' : 'true';
  }
  return undefined;
}

class Query {
  protected constructor(public readonly expressions: Array<Expression>) {

  }

  static parse(cursor: Scanner): Query {
    const result = new Array<Expression>();
    takeWhitespace(cursor);
    // eslint-disable-next-line no-constant-condition
    while (true) {
      result.push(Expression.parse(cursor));
      takeWhitespace(cursor);
      if (cursor.kind === Kind.AndKeyword) {
        cursor.take(); // consume and
        continue;
      }
      // the next token is not an 'and', so we bail now.
      return new Query(result);
    }
  }

}

class Expression {
  protected constructor(protected readonly featureToken: Token, protected readonly constantToken: Token | undefined, public readonly not: boolean) {

  }
  get feature() {
    return this.featureToken.text;
  }
  get constant() {
    return this.constantToken?.stringValue || this.constantToken?.text || undefined;
  }


  /** @internal */
  static parse(cursor: Scanner, isNotted = false, inParen = false): Expression {
    takeWhitespace(cursor);

    switch (<any>cursor.kind) {
      case Kind.Identifier: {
        // start of an expression
        const feature = cursor.take();
        takeWhitespace(cursor);

        if (<any>cursor.kind === Kind.Colon) {
          cursor.take(); // consume colon;

          // we have a constant for the
          takeWhitespace(cursor);
          switch (<any>cursor.kind) {
            case Kind.NumericLiteral:
            case Kind.BooleanLiteral:
            case Kind.Identifier:
            case Kind.StringLiteral: {
              // we have a good const value.
              const constant = cursor.take();
              return new Expression(feature, constant, isNotted);
            }
          }
          throw new MediaQueryError(i`Expected one of {Number, Boolean, Identifier, String}, found token ${JSON.stringify(cursor.text)}`, cursor.position.line, cursor.position.column);
        }
        return new Expression(feature, undefined, isNotted);
      }

      case Kind.NotKeyword:
        if (isNotted) {
          throw new MediaQueryError(i`Expression specified NOT twice`, cursor.position.line, cursor.position.column);
        }
        cursor.take(); // suck up the not token
        return Expression.parse(cursor, true, inParen);

      case Kind.OpenParen: {
        cursor.take();
        const result = Expression.parse(cursor, isNotted, inParen);
        takeWhitespace(cursor);
        if (cursor.kind !== Kind.CloseParen) {
          throw new MediaQueryError(i`Expected close parenthesis for expression, found ${JSON.stringify(cursor.text)}`, cursor.position.line, cursor.position.column);
        }

        cursor.take();
        return result;
      }

      default:
        throw new MediaQueryError(i`Expected expression, found ${JSON.stringify(cursor.text)}`, cursor.position.line, cursor.position.column);
    }
  }
}
