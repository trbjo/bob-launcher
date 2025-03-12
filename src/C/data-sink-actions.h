#ifndef DATASINK_ACTIONS_H
#define DATASINK_ACTIONS_H

typedef struct _BobLauncherAction BobLauncherAction;
typedef struct _ActionSet ActionSet;

void action_set_add_action(ActionSet* self, BobLauncherAction* action);

#endif /* DATASINK_ACTIONS_H */
