import Joi from 'joi';

export const CONFIG_VALIDATION_SCHEMA = {
    TESTLIB_CONTROL_HOST: Joi.string().default('localhost').required(),
    TESTLIB_CONTROL_PORT: Joi.number().default(3001).required(),
    TESTLIB_AGENT_NODEID: Joi.number().required(),
    TESTLIB_ENVIRONMENT_KEY: Joi.string().required(),
    TESTLIB_AGENT_TOKEN: Joi.string().required(),
    TESTLIB_COMMANDS_PATH: Joi.string().default('/opt/testlib/jobs').required(),
};
