#include "req_handlers.h"
#include "proto_objects.h"
#include "server.h"
#include "server_map.h"
#include "version.h"
#include <stdio.h>

const char* req_handle_query_option_main_thread(struct server_main_thread_runnable_args* args)
{
    struct player_query_result result = {};

    if (server_python_player_query_option(&args->state->state->server_python, args->state,
        args->query_option.option, args->query_option.action, &result))
    {
        return "Cannot process option";
    }

    if (result.message)
    {
        client_printf(args->state, "query option %d action %s w\\ follow-up query\n",
            args->query_option.option, args->query_option.action);

        // first object has a bunch of actions
        {
            ProtoStackObjectProperty actions[result.actions_count];
            memset(&actions, 0, sizeof(actions));
            ProtoStackObjectProperty* prev = NULL;

            struct player_query_action_t* action = result.actions;
            size_t i = 0;
            while (action)
            {
                ProtoStackObjectProperty* prop = &actions[i++];
                prop->key = 'a';
                prop->value = action->action;
                prop->value_size = strlen(action->action);

                prop->prev = prev;
                prev = prop;
                action = action->next;
            }

            declare_str_property_on_stack(cancel_action, 'x', result.cancel_action, prev);
            declare_str_property_on_stack(message, 'm', result.message, &cancel_action);
            uint8_t cc = result.current_option;
            declare_arg_property_on_stack(current_option, 'c', cc, &message);

            uint8_t primary = 0;
            uint8_t secondary = 0;

            {
                struct player_query_option_t* option = result.options;
                while (option)
                {
                    if (option->secondary)
                    {
                        secondary = 1;
                    }
                    else
                    {
                        primary = 1;
                    }
                    option = option->next;
                }
            }

            if (secondary && (!primary))
            {
                struct player_query_option_t* option = result.options;
                while (option)
                {
                    option->secondary = 0;
                    option = option->next;
                }

                secondary = 0;
            }

            declare_arg_property_on_stack(_secondary, 's', secondary, &current_option);
            declare_arg_property_on_stack(edit, 'e', result.edit, &_secondary);
            declare_arg_property_on_stack(quick_cancel, 'q', result.quick_cancel, &edit);

            if (result.description)
            {
                declare_str_property_on_stack(desctiption, 'd', result.description, &quick_cancel);
                declare_variable_property_on_stack(image, 'I', result.image, result.image_size, &desctiption);
                declare_arg_property_on_stack(flags, 'f', result.flags, &image);
                server_request_add_response(args->response, proto_object_allocate(&flags));
            }
            else
            {
                declare_variable_property_on_stack(image, 'I', result.image, result.image_size, &quick_cancel);
                declare_arg_property_on_stack(flags, 'f', result.flags, &image);
                server_request_add_response(args->response, proto_object_allocate(&flags));
            }
        }

        {
            struct player_query_option_t* option = result.options;
            uint8_t i = 0;
            while (option)
            {
                declare_arg_property_on_stack(id, 'i', i, NULL);
                declare_arg_property_on_stack(icon, 'c', option->icon, &id);
                declare_arg_property_on_stack(full_icon, 'c', option->full_icon, &id);
                declare_arg_property_on_stack(secondary, 's', option->secondary, option->has_full_icon ? &full_icon : &icon);
                declare_str_property_on_stack(o, 'o', option->option, &secondary);
                server_request_add_response(args->response, proto_object_allocate(&o));
                option = option->next;
                i++;
            }
        }

        server_python_player_free_query_result(&result);
    }
    else
    {
        client_printf(args->state, "query option %d action %s [closed]\n",
            args->query_option.option, args->query_option.action);

        uint8_t close_up = 0xFF;
        declare_arg_property_on_stack(_close_up, 'c', close_up, NULL);
        server_request_add_response(args->response, proto_object_allocate(&_close_up));
    }

    return NULL;
}

const char* req_handle_query_option(struct client_state_t* state, struct request_handler_response_chain_t** response)
{
    if (state->py_last_query_response == NULL)
    {
        return "Incorrect state";
    }

    uint8_t option = get_uint8_property(state->receiving_objects[0], 'o', 0);
    ProtoObjectProperty* action = find_property(state->receiving_objects[0], 'a');

    struct server_main_thread_runnable_args post_args;

    if (action)
    {
        char* a = copy_str_property(action);

        post_args.query_option.option = option;
        strncpy(post_args.query_option.action, a, sizeof(post_args.query_option.action));

        free(a);
    }
    else
    {
        // close (cancel) action
        strcpy(post_args.query_option.action, "");
    }

    post_args.state = state;
    post_args.response = response;

    return server_state_post_runnable_wait(state->state, req_handle_query_option_main_thread, post_args, &state->post_wait);
}
