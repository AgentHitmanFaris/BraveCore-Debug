# Brave Core

Brave Core is a set of changes, APIs, and scripts used for customizing Chromium to make the Brave browser. Please also check https://github.com/brave/brave-browser

Follow [@brave](https://twitter.com/brave) on Twitter for important announcements.

## Purpose

The main purpose of this repository is to house the core logic for Brave Browser's unique features, such as:
- **Brave Shields**: Ad and tracker blocking.
- **Brave Rewards**: Privacy-preserving ads and tipping.
- **Brave Wallet**: Built-in crypto wallet.
- **AI Chat**: Brave Leo, the AI assistant.

## Setup and Usage

To build Brave Browser, you typically need to follow the Chromium build instructions with Brave-specific configurations.

1.  **Clone the repository**:
    ```bash
    git clone https://github.com/brave/brave-core.git
    ```
2.  **Install dependencies**: Follow the instructions in `docs/` or the main `brave-browser` repo for setting up the build environment (depot_tools, etc.).
3.  **Build**: Use `gn` to generate build files and `ninja` to compile.
    ```bash
    gn gen out/Default
    ninja -C out/Default chrome
    ```

For detailed guides, refer to:
- [Documentation and guides](https://github.com/brave/brave-core/blob/master/docs/README.md)
- [Wiki](https://github.com/brave/brave-browser/wiki)

## Ad and Tracker Blocking

Brave Shields provides robust protection against ads and trackers. The core logic resides in `components/brave_shields`.

### How it works
The ad blocking engine is implemented in Rust (`components/brave_shields/core/browser/adblock/rs`) for performance and safety. It uses filter lists (like EasyList) to match network requests against blocking rules.

1.  **Filter Lists**: The browser downloads and maintains a set of filter lists.
2.  **Matching**: When a network request is made, the `Engine` (`components/brave_shields/core/browser/adblock/rs/src/engine.rs`) checks the URL and context against the active rules.
3.  **Action**: If a match is found, the request can be blocked, redirected, or modified (e.g., stripping cookies).
4.  **Cosmetic Filtering**: The engine also provides CSS selectors to hide ad elements on the page that cannot be blocked at the network level.

### Key Components
-   **Engine**: The main Rust struct that holds the filter lists and performs matching.
-   **FilterSet**: Manages the collection of filter lists.
-   **AdBlockServiceHelper**: C++ helper functions for integrating the Rust engine with the browser.

## AI Integration

Brave includes an AI assistant, Leo, powered by `components/ai_chat`.

### Building with AI
To build features that leverage AI in Brave:
1.  **Service**: Use `AIChatService` to manage conversations and interactions with the AI models.
2.  **Models**: The service supports multiple models (e.g., Llama, Claude). You can configure which model to use via `ModelService`.
3.  **Prompt Engineering**: Use the conversation history and context to construct effective prompts.

**Prompt to build with AI:**
"You are an intelligent assistant integrated into the browser. Your goal is to help the user browse the web more efficiently, summarize content, and answer questions based on the current page context while respecting user privacy."

## Resources

- [Issues](https://github.com/brave/brave-browser/issues)
- [Releases](https://github.com/brave/brave-browser/releases)
- [Documentation and guides](https://github.com/brave/brave-core/blob/master/docs/README.md)
- [Wiki](https://github.com/brave/brave-browser/wiki)

## Community

You can ask questions and interact with the community in the following
locations:
- [Brave Community](https://community.brave.app/)
- [`community`](https://bravesoftware.slack.com) channel on Brave Software's Slack
