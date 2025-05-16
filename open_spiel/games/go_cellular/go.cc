// Copyright 2019 DeepMind Technologies Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "open_spiel/games/go_cellular/go.h"

#include <sstream>

#include "open_spiel/game_parameters.h"
#include "open_spiel/games/go_cellular/go_board.h"
#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace go_cellular {
namespace {

// Facts about the game
const GameType kGameType{
    /*short_name=*/"go_cellular",
    /*long_name=*/"Cellular Automata Go",
    GameType::Dynamics::kSequential,
    GameType::ChanceMode::kDeterministic,
    GameType::Information::kPerfectInformation,
    GameType::Utility::kZeroSum,
    GameType::RewardModel::kTerminal,
    /*max_num_players=*/2,
    /*min_num_players=*/2,
    /*provides_information_state_string=*/true,
    /*provides_information_state_tensor=*/false,
    /*provides_observation_string=*/true,
    /*provides_observation_tensor=*/true,
    /*parameter_specification=*/
    // {{"komi", GameParameter(7.5)},
   {{"komi", GameParameter(3.5)},
     {"board_size", GameParameter(19)},
     {"handicap", GameParameter(0)},
     {"param_a", GameParameter(5)},
     {"param_b", GameParameter(3)},
     // After the maximum game length, the game will end arbitrarily and the
     // score is computed as usual (i.e. number of stones + komi).
     // It's advised to only use shorter games to compute win-rates.
     // When not provided, it defaults to DefaultMaxGameLength(board_size)
     {"max_game_length",
      GameParameter(GameParameter::Type::kInt, /*is_mandatory=*/false)}},
};

std::shared_ptr<const Game> Factory(const GameParameters& params) {
  return std::shared_ptr<const Game>(new GoGame(params));
}

REGISTER_SPIEL_GAME(kGameType, Factory);

RegisterSingleTensorObserver single_tensor(kGameType.short_name);

std::vector<VirtualPoint> HandicapStones(int num_handicap) {
  if (num_handicap < 2 || num_handicap > 9) return {};

  static std::array<VirtualPoint, 9> placement = {
      {MakePoint("d4"), MakePoint("q16"), MakePoint("d16"), MakePoint("q4"),
       MakePoint("d10"), MakePoint("q10"), MakePoint("k4"), MakePoint("k16"),
       MakePoint("k10")}};
  static VirtualPoint center = MakePoint("k10");

  std::vector<VirtualPoint> points;
  points.reserve(num_handicap);
  for (int i = 0; i < num_handicap; ++i) {
    points.push_back(placement[i]);
  }

  if (num_handicap >= 5 && num_handicap % 2 == 1) {
    points[num_handicap - 1] = center;
  }

  return points;
}

}  // namespace

GoState::GoState(std::shared_ptr<const Game> game, int board_size, float komi,
                 int handicap)
    : State(std::move(game)),
      board_(board_size),
      komi_(komi),
      handicap_(handicap),
      max_game_length_(game_->MaxGameLength()),
      to_play_(GoColor::kBlack) {
  ResetBoard();
}

std::string GoState::InformationStateString(int player) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  return HistoryString();
}

std::string GoState::ObservationString(int player) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  return ToString();
}

void GoState::ObservationTensor(int player, absl::Span<float> values) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);

  int num_cells = board_.board_size() * board_.board_size();
  SPIEL_CHECK_EQ(values.size(), num_cells * (CellStates() + 1));
  std::fill(values.begin(), values.end(), 0.);

  // Add planes: black, white, empty.
  int cell = 0;
  for (VirtualPoint p : BoardPoints(board_.board_size())) {
    int color_val = static_cast<int>(board_.PointColor(p));
    values[num_cells * color_val + cell] = 1.0;
    ++cell;
  }
  SPIEL_CHECK_EQ(cell, num_cells);

  // Add a fourth binary plane for komi (whether white is to play).
  std::fill(values.begin() + (CellStates() * num_cells), values.end(),
            (to_play_ == GoColor::kWhite ? 1.0 : 0.0));
}

std::vector<Action> GoState::LegalActions() const {
  std::vector<Action> actions{};
  if (IsTerminal()) return actions;
  
  int total_positions = board_.board_size() * board_.board_size();
  
  // 为每个规则类型生成合法动作
  for (int rule_type = 0; rule_type < static_cast<int>(GoRule::kNumRuleTypes); rule_type++) {
    // 生成所有合法落子位置的动作
    for (VirtualPoint p : BoardPoints(board_.board_size())) {
      if (board_.IsLegalMove(p, to_play_)) {
        // 计算基础动作
        Action base_action = board_.VirtualActionToAction(p,(GoRule)rule_type);
        actions.push_back(base_action);
      }
    }
    
  // 加入pass动作 - 简单地使用board_.pass_action()作为基础
  Action base_pass = board_.pass_action();
  Action pass_with_rule = rule_type * (total_positions + 1) + base_pass;
  actions.push_back(pass_with_rule);

  }
  
  return actions;
}


std::string GoState::ActionToString(Player player, Action action) const {
  // 提取规则信息
  GoRule rule = ActionToRule(action, board_.board_size());
  
  // 提取虚拟点
  VirtualPoint vp = board_.ActionToVirtualAction(action);
  
  // 组合玩家颜色、动作位置和规则信息
  return absl::StrCat(
      GoColorToString(static_cast<GoColor>(player)), " ",
      VirtualPointToString(vp), " (",
      GoRuleToString(rule), ")");
}


std::string GoState::ToString() const {
  std::stringstream ss;
  ss << "GoState(komi=" << komi_ << ", to_play=" << GoColorToString(to_play_)
     << ", history.size()=" << history_.size() << ")\n";
  ss << board_;
  return ss.str();
}

bool GoState::IsTerminal() const {
  if (history_.size() < 2) return false;
  return (history_.size() >= max_game_length_) || superko_ ||
         ((history_[history_.size() - 1].action == board_.pass_action() ||  history_[history_.size() - 1].action == 1*(board_.pass_action() + 1) + board_.pass_action()||history_[history_.size() - 1].action == 2*(board_.pass_action() + 1) + board_.pass_action())&&
          (history_[history_.size() - 2].action == board_.pass_action()|| history_[history_.size() - 2].action == 1*(board_.pass_action() + 1) + board_.pass_action()||history_[history_.size() - 2].action == 2*(board_.pass_action() + 1) + board_.pass_action()));
}

std::vector<double> GoState::Returns() const {
  if (!IsTerminal()) return {0.0, 0.0};

  if (superko_) {
    // Superko rules (https://senseis.xmp.net/?Superko) are complex and vary
    // between rulesets.
    // For simplicity and because superkos are very rare, we just treat them as
    // a draw.
    return {DrawUtility(), DrawUtility()};
  }

  // Score with Tromp-Taylor.
  float black_score = TrompTaylorScore(board_, komi_, handicap_);

  std::vector<double> returns(go_cellular::NumPlayers());
  if (black_score > 0) {
    returns[ColorToPlayer(GoColor::kBlack)] = WinUtility();
    returns[ColorToPlayer(GoColor::kWhite)] = LossUtility();
  } else if (black_score < 0) {
    returns[ColorToPlayer(GoColor::kBlack)] = LossUtility();
    returns[ColorToPlayer(GoColor::kWhite)] = WinUtility();
  } else {
    returns[ColorToPlayer(GoColor::kBlack)] = DrawUtility();
    returns[ColorToPlayer(GoColor::kWhite)] = DrawUtility();
  }
  return returns;
}

std::unique_ptr<State> GoState::Clone() const {
  return std::unique_ptr<State>(new GoState(*this));
}

void GoState::UndoAction(Player player, Action action) {
  // We don't have direct undo functionality, but copying the board and
  // replaying all actions is still pretty fast (> 1 million undos/second).
  history_.pop_back();
  --move_number_;
  ResetBoard();
  for (auto [_, action] : history_) {
    DoApplyAction(action);
  }
}

void GoState::DoApplyAction(Action action) {
  SPIEL_CHECK_TRUE(
      board_.PlayMove(board_.ActionToVirtualAction(action), to_play_,board_.ActionToRule(action)));
  to_play_ = OppColor(to_play_);

  bool was_inserted = repetitions_.insert(board_.HashValue()).second;
  if (!was_inserted && (action != board_.pass_action()) &&
      (action != 1*(board_.pass_action() + 1) + board_.pass_action()) &&
      (action != 2*(board_.pass_action() + 1) + board_.pass_action())) {
    // We have encountered this position before.
    superko_ = true;
  }
}

void GoState::ResetBoard() {
  board_.Clear();
  if (handicap_ < 2) {
    to_play_ = GoColor::kBlack;
  } else {
    for (VirtualPoint p : HandicapStones(handicap_)) {
      board_.PlayMove(p, GoColor::kBlack, GoRule::kClassic);
    }
    to_play_ = GoColor::kWhite;
  }

  repetitions_.clear();
  repetitions_.insert(board_.HashValue());
  superko_ = false;
}

GoGame::GoGame(const GameParameters& params)
    : Game(kGameType, params),
      komi_(ParameterValue<double>("komi")),
      board_size_(ParameterValue<int>("board_size")),
      handicap_(ParameterValue<int>("handicap")),
      max_game_length_(ParameterValue<int>(
          "max_game_length", DefaultMaxGameLength(board_size_))) {}

}  // namespace go
}  // namespace open_spiel
