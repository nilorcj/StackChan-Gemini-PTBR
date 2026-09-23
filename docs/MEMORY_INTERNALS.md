# StackChan Memory Internals

This document describes the standalone SD-backed memory design used by the StackChan firmware. It does not require Hermes, embeddings, or an external summarizer for the basic runtime path.

## Scope

Implemented/runtime goals:

- capture Gemini Live input/output transcription when available;
- append raw events to `events/YYYY-MM-DD.jsonl`;
- append recent dialogue transcripts to `dialogues/YYYY-MM-DD.jsonl`;
- keep semantic summaries/facts in append-only JSONL files;
- inject bounded recent context into Gemini Live setup;
- expose local HTTP APIs for recent memory, dialogues, summaries, search, stats, summarization, and vectorization status.

Out of scope for the base standalone release:

- embeddings/vector search as a required dependency;
- synchronous network calls in the hot audio path;
- exposing private memory values through ordinary search or summaries.

## Files

```text
/app/StackChan/memory/
  events/YYYY-MM-DD.jsonl
  dialogues/YYYY-MM-DD.jsonl
  summaries.jsonl
  facts.jsonl
  profile.json
  private_memory.jsonl
  context_state.json
  index/vector_index.jsonl
```

## Lifecycle

`events` are raw touch/system/audio metadata and diagnostics. They are append-only and are not directly treated as durable semantic memory.

`dialogues` are near-raw user/assistant transcript turns when Live transcription is available. Recent dialogue context can be injected into the next Live setup within a strict character budget.

`summaries` and `facts` contain compact semantic memory produced by explicit summarization or local maintenance flows. Facts are intended to be atomic and searchable.

`private_memory.jsonl` stores explicit private values such as PINs, passwords, tokens, codes, or addresses only when the user explicitly asks the robot to remember them. Ordinary summaries/search results use markers rather than raw private values.

## JSONL robustness

Power loss may leave a partial final JSONL line. Readers parse line by line, skip invalid lines, and avoid letting one corrupt line break the whole memory store.

## Retrieval

Search combines simple lexical matches, tags, observed dates, recency, and safe summaries/facts. It never exposes raw archived dialogues or private values through ordinary search.

## Summarization

The optional summary endpoint can call Gemini `generateContent` using the configured SD API key. The prompt asks the model to preserve exact technical details and useful visual/search observations while replacing private values with safe markers.
