#pragma once

// A standard 52-card deck, as one byte per card.
//
// Kept out of SolitaireCore.h so a second card game gets a card's suit without
// including Klondike's rules or writing the same five shifts again. The
// encoding is soaked by a thousand Klondike deals.
//
//   bits 0-3   rank, 0 = ace .. 12 = king
//   bits 4-5   suit
//   bit 7      face up
//
// kNoCard is 0xFF, which cannot collide because rank 15 does not exist.
//
// THE NUMBERING IS ACE-LOW AND IT IS NOT A TRICK-TAKING ORDER.
// Klondike builds foundations ace-first, so it numbers the ace 0, and it never
// compares two cards for height. A trick-taking game does nothing else, and
// under this numbering the ace is the LOWEST number in the deck, so `a > b` on
// a raw rank is silently wrong there.
//
// Such a game converts, once, in a trick-rank function of its own, rather than
// renumbering the deck: Klondike's rank arithmetic is soaked by 536,697
// assertions, and bending a working game's core encoding to suit a new one is
// how both end up wrong.

#include <cstdint>

namespace cards {

constexpr uint8_t kNoCard = 0xFF;
constexpr int kRanks = 13;
constexpr int kSuits = 4;
constexpr int kDeck = kRanks * kSuits;

// Rank indices, named where they carry a rule. Ace-low, as above.
constexpr int kAce = 0;
constexpr int kTwo = 1;
constexpr int kJack = 10;
constexpr int kQueen = 11;
constexpr int kKing = 12;

enum class Suit : uint8_t { Clubs = 0, Diamonds = 1, Spades = 2, Hearts = 3 };

inline uint8_t makeCard(const Suit suit, const int rank) {
  return static_cast<uint8_t>((static_cast<int>(suit) << 4) | rank);
}
inline int rankOf(const uint8_t card) { return card & 0x0F; }
inline Suit suitOf(const uint8_t card) { return static_cast<Suit>((card >> 4) & 0x03); }
inline bool isFaceUp(const uint8_t card) { return (card & 0x80) != 0; }
inline uint8_t faceUp(const uint8_t card) { return static_cast<uint8_t>(card | 0x80); }
inline uint8_t faceDown(const uint8_t card) { return static_cast<uint8_t>(card & 0x7F); }

// Diamonds and hearts are the red suits. On a one-bit panel this decides
// outline versus solid rather than colour.
inline bool isRed(const uint8_t card) {
  const Suit suit = suitOf(card);
  return suit == Suit::Diamonds || suit == Suit::Hearts;
}

// The one to three characters printed in a card's corner. Static storage, so
// the caller may hold the pointer for as long as it likes.
inline const char* rankLabel(const int rank) {
  static const char* kLabels[kRanks] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
  return (rank < 0 || rank >= kRanks) ? "?" : kLabels[rank];
}

// The suit's name, for a line like "FOLLOW HEARTS".
//
// Upper case, because every other word this fork puts on a panel is: the Toybox
// cut is a display face and a lower-case word dropped into a line of capitals
// reads as a different voice rather than as emphasis.
inline const char* suitName(const Suit suit) {
  switch (suit) {
    case Suit::Clubs:
      return "CLUBS";
    case Suit::Diamonds:
      return "DIAMONDS";
    case Suit::Spades:
      return "SPADES";
    case Suit::Hearts:
      return "HEARTS";
  }
  return "?";
}

}  // namespace cards
